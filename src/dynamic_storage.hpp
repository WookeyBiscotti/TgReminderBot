#pragma once

#include <nlohmann/json.hpp>
#include <unqlite_cpp/unqlite_cpp.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

class DynamicStorage {
	using Key = std::string;

  public:
	DynamicStorage(up::db& db, std::string collection): _db(db), _collection(std::move(collection)) {
		auto c = db.make_kv_cursor_or_throw();
		c.seek_or_throw(collection, up::kv_cursor_match_direction::MATCH_GE);
		while (c.valid()) {
			auto key = c.key_or_throw();
			key = key.substr(collection.size());

			auto data = c.data_string_or_throw();
			auto js = nlohmann::json::parse(data);

			_cache.emplace(key,
			    Cache{std::chrono::steady_clock::time_point{std::chrono::seconds(js["tp"].get<uint64_t>())},
			        js.at("data")});

			c.next_or_throw();
		}
	}

	std::optional<nlohmann::json> find(const std::string& key) {
		const auto now = std::chrono::steady_clock::now();
		if (_nextVacuum < now) {
			vacuum(now);
			_nextVacuum = now + std::chrono::seconds(1000);
		}

		return findImpl(key);
	}

	void removeCache(const Key& key) {
		_cache.erase(key);
		_db.remove_or_throw(makeKey(key));
	}

	void makeCache(const Key& key, nlohmann::json data, std::uint64_t timeout = 1000) {
		const auto now = std::chrono::steady_clock::now();
		if (_cache.count(key)) {
			_db.remove_or_throw(makeKey(key));
		}
		_cache.insert_or_assign(key, Cache{now + std::chrono::seconds(timeout), std::move(data)});
		nlohmann::json d;
		d["data"] = data;
		d["tp"] =
		    std::chrono::duration_cast<std::chrono::seconds>((now + std::chrono::seconds(timeout)).time_since_epoch())
		        .count();
		_db.store_or_throw(makeKey(key), d.dump());
	}

  private:
	std::optional<nlohmann::json> findImpl(const Key& key) {
		auto found = _cache.find(key);
		if (found == _cache.end()) {
			return {};
		}

		return found->second.data;
	}

	void vacuum(std::chrono::steady_clock::time_point now) {
		for (auto cacheIt = _cache.begin(); cacheIt != _cache.end();) {
			if (cacheIt->second.deadPoint < now) {
				cacheIt = _cache.erase(cacheIt);
			} else {
				++cacheIt;
			}
		}
	}

  private:
	Key makeKey(const std::string& k) { return _collection + k; }

  private:
	up::db& _db;
	const std::string _collection;

	std::chrono::steady_clock::time_point _nextVacuum{};
	struct Cache {
		std::chrono::steady_clock::time_point deadPoint;
		nlohmann::json data;
	};

	std::unordered_map<Key, Cache> _cache;
};
