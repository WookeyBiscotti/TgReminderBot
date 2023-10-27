#pragma once

#include "dynamic_storage.hpp"
#include "reminder_info.hpp"
#include "utils.hpp"
#include <bitset>

inline void sendAutoReminderMsg(TgBot::Bot& bot, std::int64_t chatId, const std::string& msg) {
	auto k = std::make_shared<TgBot::InlineKeyboardMarkup>();
	setButton(k, 0, 0, makeButon("❌ Отмена", "/delete_me"));
	setButton(k, 1, 0, makeButon("✅ Создать", "/ar_date"));

	bot.getApi().sendMessage(chatId, msg, false, 0, k);
}

inline auto queryFormatToDateTime(std::string cmd) {
	using namespace std::chrono;

	std::vector<std::string> args;
	boost::split(args, cmd, [](char c) { return c == '_'; });

	auto tod = [](auto& ts) {
		std::vector<std::string> args;
		boost::split(args, ts, [](char c) { return c == '/'; });

		return date::time_of_day<minutes>(minutes(std::stoi(args.at(0)) * 60 + std::stoi(args.at(1))));
	}(args.at(0));

	auto ymd = [](auto& ts) {
		std::vector<std::string> args;
		boost::split(args, ts, [](char c) { return c == '/'; });

		return date::year_month_day(date::day(std::stoi(args.at(0))) / std::stoi(args.at(1)) / std::stoi(args.at(2)));
	}(args.at(1));

	return std::make_pair(ymd, tod);
}

inline auto ymdFromTp(time_point_s localTp) {
	using namespace std::chrono;

	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};

	return ymd;
}

inline auto todFromTp(time_point_s localTp) {
	using namespace std::chrono;

	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	return tod;
}

inline auto ymdTodFromTp(time_point_s localTp) {
	using namespace std::chrono;

	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	return std::make_pair(ymd, tod);
}

inline std::string dateTimeToQueryFormat(date::year_month_day ymd, date::time_of_day<std::chrono::minutes> tod) {
	return fmt::format("{}/{}_{}/{}/{}", tod.hours().count(), tod.minutes().count(), static_cast<unsigned>(ymd.day()),
	    static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
}

inline std::string arDateStr(date::year_month_day ymd) {
	return fmt::format("{}/{}/{}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()),
	    static_cast<int>(ymd.year()));
}
inline date::year_month_day arStrDate(const std::string& str) {
	using namespace std::chrono;

	std::vector<std::string> args;
	boost::split(args, str, [](char c) { return c == '/'; });

	return date::year_month_day(date::day(std::stoi(args.at(0))) / std::stoi(args.at(1)) / std::stoi(args.at(2)));
}

inline std::string arTimeStr(date::time_of_day<std::chrono::minutes> tod) {
	return fmt::format("{}:{}", static_cast<unsigned>(tod.hours().count()),
	    static_cast<unsigned>(tod.minutes().count()));
}
inline date::time_of_day<std::chrono::minutes> arStrTime(const std::string& str) {
	using namespace std::chrono;

	std::vector<std::string> args;
	boost::split(args, str, [](char c) { return c == ':'; });

	return date::time_of_day<minutes>{
	    date::floor<minutes>(hours(std::stoi(args.at(0))) + minutes(std::stoi(args.at(1))))};
}
template<class D>
inline date::time_of_day<std::chrono::minutes> todAdd(date::time_of_day<std::chrono::minutes> tod, D d) {
	using namespace std::chrono;

	auto newDur = tod.to_duration() + d;
	if (newDur > date::days(1)) {
		newDur -= date::days(1);
	} else if (newDur < date::days(0)) {
		newDur += date::days(1);
	}

	return date::time_of_day<std::chrono::minutes>(date::floor<minutes>(newDur));
}

inline std::string tpToQueryFormat(time_point_s localTp) {
	using namespace std::chrono;
	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	return dateTimeToQueryFormat(ymd, tod);
}

inline TgBot::InlineKeyboardMarkup::Ptr makeArDateKeyboard(date::year_month_day ymd) {
	std::stringstream monthName;
	monthName << ymd.month();

	std::stringstream yearName;
	yearName << ymd.year();

	auto k = std::make_shared<TgBot::InlineKeyboardMarkup>();
	setButton(k, 0, 0, makeButon("<5", fmt::format("/ar_date {}", arDateStr(ymd - date::years(5)))));
	setButton(k, 1, 0, makeButon("<1", fmt::format("/ar_date {}", arDateStr(ymd - date::years(1)))));
	setButton(k, 2, 0, makeButon(yearName.str(), "_"));
	setButton(k, 3, 0, makeButon("1>", fmt::format("/ar_date {}", arDateStr(ymd + date::years(1)))));
	setButton(k, 4, 0, makeButon("5>", fmt::format("/ar_date {}", arDateStr(ymd + date::years(5)))));

	setButton(k, 0, 1, makeButon("<6", fmt::format("/ar_date {}", arDateStr(ymd - date::months(6)))));
	setButton(k, 1, 1, makeButon("<1", fmt::format("/ar_date {}", arDateStr(ymd - date::months(1)))));
	setButton(k, 2, 1, makeButon(monthName.str(), "_"));
	setButton(k, 3, 1, makeButon("1>", fmt::format("/ar_date {}", arDateStr(ymd + date::months(1)))));
	setButton(k, 4, 1, makeButon("6>", fmt::format("/ar_date {}", arDateStr(ymd + date::months(6)))));

	setButton(k, 0, 2, makeButon("Пн", "_"));
	setButton(k, 1, 2, makeButon("Вт", "_"));
	setButton(k, 2, 2, makeButon("Ср", "_"));
	setButton(k, 3, 2, makeButon("Чт", "_"));
	setButton(k, 4, 2, makeButon("Пт", "_"));
	setButton(k, 5, 2, makeButon("Сб", "_"));
	setButton(k, 6, 2, makeButon("Вс", "_"));

	int todayDay = static_cast<unsigned>(ymd.day());
	int lastDay = static_cast<unsigned>(date::year_month_day_last(ymd.year(), date::month_day_last(ymd.month())).day());
	int firstWeekDay = date::year_month_weekday(date::day(1) / ymd.month() / ymd.year()).weekday().iso_encoding() - 1;
	int currDay = 0;

	for (int i = 0; i != 50; ++i) {
		int x = i % 7;
		int y = i / 7;
		if (i >= firstWeekDay && currDay < lastDay) {
			if (currDay + 1 == todayDay) {
				setButton(k, x, y + 3, makeButon(fmt::format("|{}|", currDay + 1), "_"));
			} else {
				setButton(k, x, y + 3,
				    makeButon(std::to_string(currDay + 1),
				        fmt::format("/ar_date {}",
				            arDateStr(date::year_month_day(date::day(currDay + 1) / ymd.month() / ymd.year())))));
			}
			currDay++;
		} else {
			setButton(k, x, y + 3, makeButon(" ", "_"));
			if (x == 6) {
				break;
			}
		}
	}
	const auto lastRow = k->inlineKeyboard.size();
	setButton(k, 0, lastRow, makeButon("❌ Отмена", "/delete_me"));
	setButton(k, 1, lastRow, makeButon("➡️ Далее ", "/ar_time"));

	return k;
}

inline TgBot::InlineKeyboardMarkup::Ptr makeArTimeKeyboard(date::time_of_day<std::chrono::minutes> tod) {
	using namespace std::chrono;

	auto k = std::make_shared<TgBot::InlineKeyboardMarkup>();
	setButton(k, 0, 0, makeButon("+6", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, hours(6))))));
	setButton(k, 1, 0, makeButon("+3", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, hours(3))))));
	setButton(k, 2, 0, makeButon("+1", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, hours(1))))));
	setButton(k, 3, 0, makeButon("+15", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, minutes(15))))));
	setButton(k, 4, 0, makeButon("+5", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, minutes(5))))));
	setButton(k, 5, 0, makeButon("+1", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, minutes(1))))));

	setButton(k, 0, 1, makeButon(std::to_string(tod.hours().count()), "_"));
	setButton(k, 1, 1, makeButon(std::to_string(tod.minutes().count()), "_"));

	setButton(k, 0, 2, makeButon("-6", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -hours(6))))));
	setButton(k, 1, 2, makeButon("-3", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -hours(3))))));
	setButton(k, 2, 2, makeButon("-1", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -hours(1))))));
	setButton(k, 3, 2, makeButon("-15", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -minutes(15))))));
	setButton(k, 4, 2, makeButon("-5", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -minutes(5))))));
	setButton(k, 5, 2, makeButon("-1", fmt::format("/ar_time {}", arTimeStr(todAdd(tod, -minutes(1))))));

	const auto lastRow = k->inlineKeyboard.size();
	setButton(k, 0, lastRow, makeButon("❌ Отмена", "/delete_me"));
	setButton(k, 1, lastRow, makeButon("⬅️ Назад ", "/ar_date"));
	setButton(k, 2, lastRow, makeButon("➡️ Далее ", "/ar_repeat"));

	return k;
}

inline auto ar_date(TgBot::Bot& bot, DynamicStorage& ds) {
	return [&](TgBot::CallbackQuery::Ptr query) {
		try {
			if (!query->message) {
				return;
			}
			auto [userId, chatId] = getUserChatOrThrow(query->message);

			auto dsKey = chatMsgKey(chatId, query->message->messageId);
			auto args = split(query->data);
			if (args.empty()) {
				throw std::runtime_error("Нет команды");
			} else if (args.size() == 1) {
				auto found = ds.find(dsKey);
				if (found) {
					args.push_back(found->at("date").get_string_or_throw());
				} else {
					ds.make(dsKey, up::value::object{});
					args.push_back(arDateStr(ymdFromTp(now())));
				}
			}

			auto state = ds.find(dsKey);
			if (!state) {
				throw std::runtime_error("No state");
			}
			(*state)["date"] = args[1];
			(*state)["text"] = query->message->text;
			ds.make(dsKey, *state);

			auto ymd = arStrDate(args[1]);
			auto k = makeArDateKeyboard(ymd);

			bot.getApi().editMessageText(query->message->text, chatId, query->message->messageId, "", "", false, k);
		} catch (const std::exception& e) {
			if (query->message->chat) {
				bot.getApi().sendMessage(query->message->chat->id, e.what());
			}
		}
	};
}

inline auto ar_time(TgBot::Bot& bot, DynamicStorage& ds) {
	return [&](TgBot::CallbackQuery::Ptr query) {
		try {
			if (!query->message) {
				return;
			}
			auto [userId, chatId] = getUserChatOrThrow(query->message);

			auto dsKey = chatMsgKey(chatId, query->message->messageId);
			auto args = split(query->data);
			if (args.empty()) {
				throw std::runtime_error("Нет команды");
			} else if (args.size() == 1) {
				auto found = ds.find(dsKey);
				if (!found || !found->is_object()) {
					throw std::runtime_error("internal error !found");
				}
				if (!found->contains("time")) {
					args.push_back(arTimeStr(todFromTp({})));
				} else {
					args.push_back(found->at("time").get_string());
				}
			}

			auto state = ds.find(dsKey);
			if (!state) {
				throw std::runtime_error("No state");
			}
			(*state)["time"] = args[1];
			ds.make(dsKey, *state);

			auto tod = arStrTime(args[1]);
			auto k = makeArTimeKeyboard(tod);

			bot.getApi().editMessageText(query->message->text, chatId, query->message->messageId, "", "", false, k);
		} catch (const std::exception& e) {
			if (query->message->chat) {
				bot.getApi().sendMessage(query->message->chat->id, e.what());
			}
		}
	};
}

struct Repeating {
	up::value all;
	int d = 0;
	int m = 0;
	int y = 0;
	std::bitset<7> w = 0;

	std::string to_string() const {
		if (d != 0) {
			return "d" + std::to_string(d);
		} else if (m != 0) {
			return "m" + std::to_string(m);
		} else if (y != 0) {
			return "y" + std::to_string(y);
		} else if (w.to_ulong() != 0) {
			return "w" + std::to_string(w.to_ulong());
		} else {
			return "n";
		}
	}

	std::string to_command_string() const {
		if (d != 0) {
			return "d" + std::to_string(d);
		} else if (m != 0) {
			return "m" + std::to_string(m);
		} else if (y != 0) {
			return "y" + std::to_string(y);
		} else if (w.to_ulong() != 0) {
			std::string s = "w";
			for (size_t i = 0; i != 7; ++i) {
				if (w[i]) {
					s += std::to_string(i + 1);
				}
			}
			return s;
		} else {
			return "n";
		}
	}

	static Repeating from_string(std::string str) {
		if (str.empty() || str.front() == 'n') {
			return Repeating{};
		}
		const auto c = str[0];
		str.erase(str.begin());

		const int count = std::max<int>(0, str.empty() ? 0 : std::stoul(str));

		if (c == 'd') {
			return Repeating{.d = count};
		} else if (c == 'm') {
			return Repeating{.m = count};
		} else if (c == 'y') {
			return Repeating{.y = count};
		} else if (c == 'w') {
			return Repeating{.w = count};
		} else {
			return {};
		}
	}

	bool noRepeat() const { return d == 0 && m == 0 && y == 0 && w.none(); }
};

inline TgBot::InlineKeyboardMarkup::Ptr makeArRepeatKeyboard(Repeating rp) {
	using namespace std::chrono;

	auto k = std::make_shared<TgBot::InlineKeyboardMarkup>();
	if (rp.noRepeat()) {
		setButton(k, 0, 0, makeButon("|Без повторений|", "_"));
	} else {
		setButton(k, 0, 0, makeButon("Без повторений", "/ar_repeat n"));
	}

	setButton(k, 0, 1, makeButon("-10", rp.d ? fmt::format("/ar_repeat d{}", rp.d - 10) : " "));
	setButton(k, 1, 1, makeButon("-1", rp.d ? fmt::format("/ar_repeat d{}", rp.d - 1) : " "));
	if (rp.d == 0) {
		setButton(k, 2, 1, makeButon(fmt::format("{} Дней", rp.d), "_"));
	} else {
		setButton(k, 2, 1, makeButon(fmt::format("|{} Дней|", rp.d), "_"));
	}
	setButton(k, 3, 1, makeButon("+1", fmt::format("/ar_repeat d{}", rp.d + 1)));
	setButton(k, 4, 1, makeButon("+10", fmt::format("/ar_repeat d{}", rp.d + 10)));

	setButton(k, 0, 2, makeButon("-10", rp.m ? fmt::format("/ar_repeat m{}", rp.m - 10) : " "));
	setButton(k, 1, 2, makeButon("-1", rp.m ? fmt::format("/ar_repeat m{}", rp.m - 1) : " "));
	if (rp.m == 0) {
		setButton(k, 2, 2, makeButon(fmt::format("{} Месяцев", rp.m), "_"));
	} else {
		setButton(k, 2, 2, makeButon(fmt::format("|{} Месяцев|", rp.m), "_"));
	}
	setButton(k, 3, 2, makeButon("+1", fmt::format("/ar_repeat m{}", rp.m + 1)));
	setButton(k, 4, 2, makeButon("+10", fmt::format("/ar_repeat m{}", rp.m + 10)));

	setButton(k, 0, 3, makeButon("-10", rp.y ? fmt::format("/ar_repeat y{}", rp.y - 10) : " "));
	setButton(k, 1, 3, makeButon("-1", rp.y ? fmt::format("/ar_repeat y{}", rp.y - 1) : " "));
	if (rp.y == 0) {
		setButton(k, 2, 3, makeButon(fmt::format("{} Лет", rp.y), "_"));
	} else {
		setButton(k, 2, 3, makeButon(fmt::format("|{} Лет|", rp.y), "_"));
	}
	setButton(k, 3, 3, makeButon("+1", fmt::format("/ar_repeat y{}", rp.y + 1)));
	setButton(k, 4, 3, makeButon("+10", fmt::format("/ar_repeat y{}", rp.y + 10)));

	if (!rp.w[0]) {
		setButton(k, 0, 4, makeButon("Пн", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b1)));
	} else {
		setButton(k, 0, 4, makeButon("|Пн|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1111110)));
	}
	if (!rp.w[1]) {
		setButton(k, 1, 4, makeButon("Вт", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b10)));
	} else {
		setButton(k, 1, 4, makeButon("|Вт|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1111101)));
	}
	if (!rp.w[2]) {
		setButton(k, 2, 4, makeButon("Ср", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b100)));
	} else {
		setButton(k, 2, 4, makeButon("|Ср|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1111011)));
	}
	if (!rp.w[3]) {
		setButton(k, 3, 4, makeButon("Чт", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b1000)));
	} else {
		setButton(k, 3, 4, makeButon("|Чт|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1110111)));
	}
	if (!rp.w[4]) {
		setButton(k, 4, 4, makeButon("Пт", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b10000)));
	} else {
		setButton(k, 4, 4, makeButon("|Пт|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1101111)));
	}
	if (!rp.w[5]) {
		setButton(k, 5, 4, makeButon("Сб", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b100000)));
	} else {
		setButton(k, 5, 4, makeButon("|Сб|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b1011111)));
	}
	if (!rp.w[6]) {
		setButton(k, 6, 4, makeButon("Вс", fmt::format("/ar_repeat w{}", rp.w.to_ulong() | 0b1000000)));
	} else {
		setButton(k, 6, 4, makeButon("|Вс|", fmt::format("/ar_repeat w{}", rp.w.to_ulong() & 0b0111111)));
	}

	const auto lastRow = k->inlineKeyboard.size();
	setButton(k, 0, lastRow, makeButon("❌ Отмена", "/delete_me"));
	setButton(k, 1, lastRow, makeButon("⬅️ Назад ", "/ar_time"));
	setButton(k, 2, lastRow,
	    makeButon("✅ Создать ", fmt::format("/add {} {} {} ", rp.all.at("date").get_string_view_or_throw(),
	                                rp.all.at("time").get_string_view_or_throw(), rp.to_command_string())));

	return k;
}

inline auto ar_repeat(TgBot::Bot& bot, DynamicStorage& ds) {
	return [&](TgBot::CallbackQuery::Ptr query) {
		try {
			if (!query->message) {
				return;
			}
			auto [userId, chatId] = getUserChatOrThrow(query->message);

			auto dsKey = chatMsgKey(chatId, query->message->messageId);
			auto args = split(query->data);
			if (args.empty()) {
				throw std::runtime_error("Нет команды");
			} else if (args.size() == 1) {
				auto found = ds.find(dsKey);
				if (!found || !found->is_object()) {
					throw std::runtime_error("internal error !found");
				}
				if (!found->contains("repeat")) {
					args.push_back(Repeating{}.to_string());
				} else {
					args.push_back(found->at("repeat").get_string());
				}
			}

			auto state = ds.find(dsKey);
			if (!state) {
				throw std::runtime_error("No state");
			}
			(*state)["repeat"] = args[1];
			ds.make(dsKey, *state);

			auto rp = Repeating::from_string(args[1]);
			rp.all = *state;
			auto k = makeArRepeatKeyboard(rp);

			bot.getApi().editMessageText(query->message->text, chatId, query->message->messageId, "", "", false, k);
		} catch (const std::exception& e) {
			if (query->message->chat) {
				bot.getApi().sendMessage(query->message->chat->id, e.what());
			}
		}
	};
}
