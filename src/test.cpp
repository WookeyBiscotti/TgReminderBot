#include <boost/algorithm/string/split.hpp>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <tgbot/Bot.h>
#include <tgbot/net/CurlHttpClient.h>
#include <tgbot/net/TgLongPoll.h>
#include <unqlite_cpp/unqlite_cpp.hpp>

#include "dynamic_storage.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace nlohmann::json_literals;

int main() {
	{
		up::db db("test.db");
		DynamicStorage ds(db, "test");

		ds.make("id1", "WTF", 1);
		std::cout << ds.find("id1")->get_string_view() << std::endl;
	}
	{
		up::db db("test.db");
		DynamicStorage ds(db, "test");

		std::cout << ds.find("id1")->get_string_view() << std::endl;
	}
	{
		up::db db("test.db");
		DynamicStorage ds(db, "test");

		ds.vacuum();
		std::cout << ds.find("id1").has_value() << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(5));
		ds.vacuum();
		std::cout << ds.find("id1").has_value() << std::endl;
	}

	return 0;
}
