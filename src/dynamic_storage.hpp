#pragma once

#include <unqlite_cpp/unqlite_cpp.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

class DynamicStorage {
	using Key = std::string;
	using Data = up::value;

  public:
	DynamicStorage(up::db& db, std::string collection): _db(db), _collection(std::move(collection)) {
		auto recs = up::vm_fetch_all_records(db).fetch_or_throw(_collection).make_value();

		recs.foreach_if_array([&](auto, const up::value& v) {
			_cache.emplace(v.at("key").get_string(),
			    Cache{std::chrono::steady_clock::time_point{std::chrono::seconds(v.at("dp").get_int())}, v.at("data"),
			        v.at("__id").get_int()});
			return true;
		});
	}

	std::optional<Data> find(const std::string& key) {
		const auto now = std::chrono::steady_clock::now();
		if (_nextVacuum < now) {
			vacuum(now);
			_nextVacuum = now + std::chrono::seconds(1000);
		}

		return findImpl(key);
	}

	void removeCache(const Key& key) {
		_cache.erase(key);
		_db.remove_or_throw(key);
	}

	void make(const Key& key, Data data, std::uint64_t timeout = 1000) {
		const auto now = std::chrono::steady_clock::now();
		if (_cache.count(key)) {
			up::vm_drop_record(_db).drop(_collection, _cache[key].id);
		}
		up::value d;
		d["key"] = key;
		d["data"] = data;
		d["dp"] =
		    std::chrono::duration_cast<std::chrono::seconds>((now + std::chrono::seconds(timeout)).time_since_epoch())
		        .count();
		auto id = up::vm_store_record(_db).store_or_throw(_collection, d);
		_cache.insert_or_assign(key, Cache{now + std::chrono::seconds(timeout), std::move(data), id});
	}

	void vacuum(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
		for (auto cacheIt = _cache.begin(); cacheIt != _cache.end();) {
			if (cacheIt->second.deadPoint < now) {
				cacheIt = _cache.erase(cacheIt);
			} else {
				++cacheIt;
			}
		}
	}

  private:
	std::optional<Data> findImpl(const Key& key) {
		auto found = _cache.find(key);
		if (found == _cache.end()) {
			return {};
		}

		return found->second.data;
	}

  private:
	up::db& _db;
	const std::string _collection;

	std::chrono::steady_clock::time_point _nextVacuum{};
	struct Cache {
		std::chrono::steady_clock::time_point deadPoint;
		Data data;
		int64_t id;
	};

	std::unordered_map<Key, Cache> _cache;
};
