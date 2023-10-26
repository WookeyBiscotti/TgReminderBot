#pragma once

#include <boost/algorithm/string/split.hpp>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <tgbot/Bot.h>
#include <tgbot/net/CurlHttpClient.h>
#include <tgbot/net/TgLongPoll.h>
#include <unqlite_cpp/unqlite_cpp.hpp>

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

using time_point_s = std::chrono::time_point<std::chrono::seconds>;

constexpr size_t MAX_MESSAGE_SIZE = 4096;

inline time_point_s now() {
	static auto zone = date::locate_zone("Europe/Moscow");

	return time_point_s(std::chrono::duration_cast<std::chrono::seconds>(
	    date::make_zoned(zone, std::chrono::system_clock::now()).get_local_time().time_since_epoch()));
}

inline TgBot::InlineKeyboardButton::Ptr makeButon(const std::string& label, const std::string& key) {
	TgBot::InlineKeyboardButton::Ptr bt(new TgBot::InlineKeyboardButton);
	bt->text = label;
	bt->callbackData = key;

	return bt;
}

inline void setButton(TgBot::InlineKeyboardMarkup::Ptr keyboard, size_t x, size_t y,
    TgBot::InlineKeyboardButton::Ptr btn) {
	if (keyboard->inlineKeyboard.size() <= y) {
		keyboard->inlineKeyboard.resize(y + 1);
	}
	if (keyboard->inlineKeyboard[y].size() <= x) {
		keyboard->inlineKeyboard[y].resize(x + 1);
	}
	keyboard->inlineKeyboard[y][x] = btn;
}

inline std::string findToken() {
	std::string token;
	std::ifstream tokenFile("token");
	if (!tokenFile.is_open()) {
		throw std::runtime_error("Can't find token");
	}
	tokenFile >> token;

	return token;
}

inline std::pair<int64_t, int64_t> getUserChatOrThrow(const TgBot::Message::Ptr& msg) {
	if (!msg->chat) {
		throw std::runtime_error("Невозможно переслать сообщение");
	}

	if (!msg->from) {
		throw std::runtime_error("Несуществующий пользователь!");
	}

	return {msg->from->id, msg->chat->id};
}

inline std::string chatMsgKey(int64_t c, int64_t m) {
	return std::to_string(c) + "_" + std::to_string(m);
}

inline auto split(const std::string& text) {
	std::vector<std::string> args;
	boost::split(args, text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

	return args;
}
