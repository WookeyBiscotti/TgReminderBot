#include <boost/algorithm/string/split.hpp>
#include <date/date.h>
#include <date/tz.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <tgbot/Bot.h>
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

using namespace std::chrono;
using namespace TgBot;

constexpr size_t MAX_MESSAGE_SIZE = 4096;

TgBot::InlineKeyboardButton::Ptr makeButon(const std::string& label, const std::string& key) {
	TgBot::InlineKeyboardButton::Ptr bt(new TgBot::InlineKeyboardButton);
	bt->text = label;
	bt->callbackData = key;

	return bt;
}

void setButton(TgBot::InlineKeyboardMarkup::Ptr keyboard, size_t x, size_t y, TgBot::InlineKeyboardButton::Ptr btn) {
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

bool eraseReminder(up::db& db, std::int64_t chatId, std::int64_t recId) {
	auto vm = db.compile_or_throw("$result = db_drop_record($c, $recId);");
	std::string collection = fmt::format("reminders_{}", chatId);
	vm.bind_or_throw("c", collection);
	vm.bind_or_throw("recId", recId);
	vm.exec_or_throw();
	db.commit_or_throw();

	return vm.extract_or_throw("result").get_bool_or_throw();
}

std::string prettyDateTime(time_point<seconds> localTp) {
	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	return fmt::format("{:0>2}:{:0>2} {:0>2}/{:0>2}/{}", tod.hours().count(), tod.minutes().count(),
	    static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
}

std::string dateTimeToQueryFormat(date::year_month_day ymd, date::time_of_day<minutes> tod) {
	return fmt::format("{}/{}_{}/{}/{}", tod.hours().count(), tod.minutes().count(), static_cast<unsigned>(ymd.day()),
	    static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
}

auto queryFormatToDateTime(std::string cmd) {
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

time_point<seconds> now() {
	static auto zone = date::locate_zone("Europe/Moscow");

	return time_point<seconds>(
	    duration_cast<seconds>(date::make_zoned(zone, system_clock::now()).get_local_time().time_since_epoch()));
}

TgBot::InlineKeyboardMarkup::Ptr makeAddIKeyboard(time_point<seconds> localTp) {
	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	std::stringstream monthName;
	monthName << ymd.month();

	std::stringstream yearName;
	yearName << ymd.year();

	auto k = std::make_shared<TgBot::InlineKeyboardMarkup>();
	// setButton(k, 0, 0, makeButon("-10", fmt::format("/addi {}", dateTimeToQueryFormat(ymd - date::years(10), tod))));
	// setButton(k, 1, 0, makeButon("-5", fmt::format("/addi {}", dateTimeToQueryFormat(ymd - date::years(5), tod))));
	// setButton(k, 2, 0, makeButon("-1", fmt::format("/addi {}", dateTimeToQueryFormat(ymd - date::years(1), tod))));
	// setButton(k, 3, 0, makeButon(yearName.str(), "_"));
	// setButton(k, 4, 0, makeButon("+1", fmt::format("/addi {}", dateTimeToQueryFormat(ymd + date::years(1), tod))));
	// setButton(k, 5, 0, makeButon("+5", fmt::format("/addi {}", dateTimeToQueryFormat(ymd + date::years(5), tod))));
	// setButton(k, 6, 0, makeButon("+10", fmt::format("/addi {}", dateTimeToQueryFormat(ymd + date::years(10), tod))));

	setButton(k, 0, 0, makeButon("-6", fmt::format("/addi {}", dateTimeToQueryFormat(ymd - date::months(6), tod))));
	setButton(k, 1, 0, makeButon("-1", fmt::format("/addi {}", dateTimeToQueryFormat(ymd - date::months(1), tod))));
	setButton(k, 2, 0, makeButon(monthName.str(), "_"));
	setButton(k, 3, 0, makeButon("+1", fmt::format("/addi {}", dateTimeToQueryFormat(ymd + date::months(1), tod))));
	setButton(k, 4, 0, makeButon("+5", fmt::format("/addi {}", dateTimeToQueryFormat(ymd + date::months(6), tod))));

	setButton(k, 0, 1, makeButon("Пн", "_"));
	setButton(k, 1, 1, makeButon("Вт", "_"));
	setButton(k, 2, 1, makeButon("Ср", "_"));
	setButton(k, 3, 1, makeButon("Чт", "_"));
	setButton(k, 4, 1, makeButon("Пт", "_"));
	setButton(k, 5, 1, makeButon("Сб", "_"));
	setButton(k, 6, 1, makeButon("Вс", "_"));

	int todayDay = static_cast<unsigned>(ymd.day());
	int lastDay = static_cast<unsigned>(date::year_month_day_last(ymd.year(), date::month_day_last(ymd.month())).day());
	int firstWeekDay = date::year_month_weekday(date::day(1) / ymd.month() / ymd.year()).weekday().iso_encoding() - 1;
	int currDay = 0;

	for (int i = 0; i != 50; ++i) {
		int x = i % 7;
		int y = i / 7;
		if (i >= firstWeekDay && currDay < lastDay) {
			setButton(k, x, y + 2,
			    makeButon(currDay + 1 == todayDay ? fmt::format("|{}|", currDay + 1) : std::to_string(currDay + 1),
			        fmt::format("/addi {}",
			            dateTimeToQueryFormat(date::year_month_day(date::day(currDay + 1) / ymd.month() / ymd.year()),
			                tod))));
			currDay++;
		} else {
			setButton(k, x, y + 2, makeButon(" ", "_"));
			if (x == 6) {
				break;
			}
		}
	}

	return k;
}

struct ReminderInfo {
	std::string descr;

	bool on = true;

	std::int64_t day = -1;
	std::int64_t month = -1;
	std::int64_t year = -1;

	std::int64_t hour = -1;
	std::int64_t minute = -1;

	bool year_repeat = false;
	std::int64_t month_repeat = 0;
	std::int64_t week_repeat = 0; // bit schema here
	std::int64_t day_repeat = 0;

	std::int64_t _id = -1;

	std::string pretty() const {
		return fmt::format("{:0>2}/{:0>2}/{} {:0>2}:{:0>2} {}{}", day, month, year, hour, minute, descr,
		    prettyRepeat());
	}

	std::string prettyRepeat() const {
		std::string repeatInfo;
		if (year_repeat) {
			repeatInfo = "\nПовтор ежегодно.";
		} else if (month_repeat != 0) {
			repeatInfo = "\nПовтор ежемесячно.";
			if (month_repeat == 1) {
				repeatInfo = "\nПовтор ежемесячно.";
			} else if (month_repeat < 5) {
				repeatInfo = fmt::format("\nПовтор каждые {} месяца.", month_repeat);
			} else {
				repeatInfo = fmt::format("\nПовтор каждые {} месяцев.", month_repeat);
			}
		} else if (day_repeat != 0) {
			if (day_repeat == 1) {
				repeatInfo = "\nПовтор ежедневно.";
			} else if (day_repeat < 5) {
				repeatInfo = fmt::format("\nПовтор каждые {} дня.", day_repeat);
			} else {
				repeatInfo = fmt::format("\nПовтор каждые {} дней.", day_repeat);
			}
		} else if (week_repeat != 0) {
			repeatInfo = "\nПовтор по:";
			const char* days[7] = {" Пн,", " Вт,", " Ср,", " Чт,", " Пт,", " Сб,", " Вс,"};
			for (size_t i = 0; i != 7; ++i) {
				if (week_repeat & (1 << i)) {
					repeatInfo += days[i];
				}
			}
			repeatInfo.back() = '.';
		}

		return repeatInfo;
	}

	std::string toString() const {
		std::string repeatInfo;
		if (year_repeat) {
			repeatInfo = 'y';
		} else if (month_repeat != 0) {
			repeatInfo = 'm';
			repeatInfo += std::to_string(month_repeat);
		} else if (day_repeat != 0) {
			repeatInfo = 'd';
			repeatInfo += std::to_string(day_repeat);
		} else if (week_repeat != 0) {
			repeatInfo = 'w';
			for (size_t i = 0; i != 7; ++i) {
				if (week_repeat & (1 << i)) {
					repeatInfo += '1' + i;
				}
			}
		}

		return fmt::format("{:0>2}/{:0>2}/{} {:0>2}:{:0>2} {} {}", day, month, year, hour, minute, repeatInfo, descr);
	}

	template<class V>
	void fromValue(const V& v) {
		descr = v.at("descr").get_string_or_throw();

		on = v.at("on").get_bool_or_throw();

		day = v.at("day").get_int_or_throw();
		month = v.at("month").get_int_or_throw();
		year = v.at("year").get_int_or_throw();

		hour = v.at("hour").get_int_or_throw();
		minute = v.at("minute").get_int_or_throw();

		year_repeat = v.at("year_repeat").get_bool_or_throw();
		if (v.at("month_repeat").is_bool()) {
			month_repeat = v.at("month_repeat").get_bool_or_throw();
		} else {
			month_repeat = v.at("month_repeat").get_int_or_throw();
		}
		week_repeat = v.at("week_repeat").get_int_or_throw();
		day_repeat = v.at("day_repeat").get_int_or_throw();

		_id = v.at("__id").get_int_or_default(-1);
	}

	void toValue(up::value& v) const {
		v["descr"] = descr;

		v["on"] = on;

		v["day"] = day;
		v["month"] = month;
		v["year"] = year;

		v["hour"] = hour;
		v["minute"] = minute;

		v["year_repeat"] = year_repeat;
		v["month_repeat"] = month_repeat;
		v["week_repeat"] = week_repeat;
		v["day_repeat"] = day_repeat;
	}

	// /add 13/06/23 14:23 n|y|m|d|w12345 msg with spaces
	bool parseCommand(const std::string& cmd, std::string& error) {
		std::vector<std::string> args;
		boost::split(args, cmd, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

		if (args.size() < 5) {
			error += "⚠️ Неверное колличество аргументов!";
			return false;
		}
		args.erase(args.begin());

		{
			std::vector<std::string> dateStrs;
			boost::split(dateStrs, args.front(), [](char c) { return c == '.' || c == '/' || c == '\\'; });
			if (dateStrs.size() != 3) {
				error += "⚠️ Неверный формат даты!";
				return false;
			}
			day = std::atoi(dateStrs[0].c_str());
			month = std::atoi(dateStrs[1].c_str());
			year = std::atoi(dateStrs[2].c_str());
			if (year < 100) {
				year += 2000;
			}
			date::year_month_day date{date::year(year), date::month(month), date::day(day)};
			if (!date.ok()) {
				error += "⚠️ Неверный формат даты!";
				return false;
			}
			args.erase(args.begin());
		}
		{
			std::vector<std::string> timeStrs;
			boost::split(timeStrs, args.front(), [](char c) { return c == '.' || c == '/' || c == '\\' || c == ':'; });
			if (timeStrs.size() != 2) {
				error += "⚠️ Неверный формат времени!";
				return false;
			}
			hour = std::atoi(timeStrs[0].c_str());
			if (hour < 0 || hour > 23) {
				error += fmt::format("⚠️ Неверный формат часа!({})", hour);
				return false;
			}
			minute = std::atoi(timeStrs[1].c_str());
			if (minute < 0 || minute > 59) {
				error += fmt::format("⚠️ Неверный формат минуты!({})", minute);
				return false;
			}
			args.erase(args.begin());
		}
		{
			auto c = args.front().front();
			if (c == 'y') {
				year_repeat = true;
			} else if (c == 'm') {
				auto numStr = args.front().substr(1);
				try {
					month_repeat = numStr.empty() ? 1 : std::stoi(numStr);
				} catch (const std::exception& e) {
					error += "⚠️ Неверный формат дней!";
					return false;
				}
			} else if (c == 'd') {
				auto numStr = args.front().substr(1);
				try {
					day_repeat = numStr.empty() ? 1 : std::stoi(numStr);
				} catch (const std::exception& e) {
					error += "⚠️ Неверный формат дней!";
					return false;
				}
			} else if (c == 'w') {
				for (size_t i = 1; i != args.front().size(); ++i) {
					auto week_day = args.front()[i] - '1';
					if (week_day < 0 || week_day > 7) {
						error += fmt::format("⚠️ Неверный формат повтора недели!({})", args.front()[i]);
						return false;
					}

					week_repeat |= 1 << week_day;
				}
			}
			args.erase(args.begin());
		}
		{
			for (const auto& w : args) {
				descr += w;
				if (&w != &args.back()) {
					descr += ' ';
				}
			}
		}

		return true;
	}

	static auto bitWeekToArray(std::int64_t bw) {
		std::array<bool, 7> res;
		for (size_t i = 0; i != 7; ++i) {
			res[i] = !!(bw & (1 << i));
		}
		return res;
	}

	time_point<seconds> getNearTs(time_point<seconds> localTp) const {
		auto toTp = [](const date::time_of_day<minutes>& t, const date::year_month_day& d) {
			return time_point<seconds>{duration_cast<seconds>(t.to_duration() + date::sys_days{d}.time_since_epoch())};
		};

		time_point<seconds> res;

		if (!on) {
			return time_point<seconds>(minutes(0));
		}

		auto remTime = date::time_of_day<minutes>(minutes(static_cast<long>(hour * 60 + minute)));
		auto remDate = date::day(day) / month / year;
		auto remTp = toTp(remTime, remDate);

		auto currTime = date::time_of_day<minutes>{floor<minutes>(localTp - floor<date::days>(localTp))};
		auto currDate = date::year_month_day{date::sys_days{floor<date::days>(localTp.time_since_epoch())}};
		auto currTp = localTp;

		if (year_repeat) {
			if (remTp > currTp) {
				return remTp;
			}
			remDate = date::day(remDate.day()) / remDate.month() / currDate.year();
			remTp = toTp(remTime, remDate);

			while (remTp <= currTp || !remDate.ok()) {
				remDate += date::years(1);
				remTp = toTp(remTime, remDate);
			}
		} else if (month_repeat != 0) {
			if (remTp > currTp) {
				return remTp;
			}
			remDate = date::day(remDate.day()) / remDate.month() / currDate.year();
			remTp = toTp(remTime, remDate);

			while (remTp <= currTp || !remDate.ok()) {
				remDate += date::months(month_repeat);
				remTp = toTp(remTime, remDate);
			}
		} else if (day_repeat != 0) {
			while (remTp < currTp) {
				remDate = date::sys_days{remDate} + date::days{day_repeat};
				remTp = toTp(remTime, remDate);
			}
		} else if (week_repeat != 0) {
			auto wa = bitWeekToArray(week_repeat);
			auto weekIndex = [](const time_point<seconds>& tp) {
				return date::year_month_weekday{date::sys_days{duration_cast<date::days>(tp.time_since_epoch())}}
				           .weekday()
				           .iso_encoding() -
				       1;
			};

			if (remTp < currTp) {
				remTp = currTp;
			}
			while (!wa[weekIndex(remTp)] || remTp <= currTp) {
				remDate = date::sys_days{remDate} + date::days{1};
				remTp = toTp(remTime, remDate);
			}
		}

		return remTp;
	}

	bool isRepeatable() const { return year_repeat || month_repeat || week_repeat || day_repeat; }
};

class ReminderQuery {
  public:
	ReminderQuery(Bot& bot): _bot(bot) {}
	void addTimer(std::int64_t chatId, time_point<seconds> tp, const ReminderInfo& reminder) {
		std::scoped_lock l(_m);
		_order[chatId].emplace(tp, reminder);
		_cond.notify_all();
	}

	void removeTimer(std::int64_t chatId, std::int64_t reminderId) {
		std::scoped_lock l(_m);
		auto& rms = _order[chatId];
		for (auto it = rms.begin(); it != rms.end();) {
			if (it->second._id != reminderId) {
				it++;
			} else {
				it = rms.erase(it);
			}
		}
		_cond.notify_all();
	}

	void stop() {
		std::scoped_lock l(_m);
		_running = false;
		_cond.notify_all();
	}

	void run() {
		struct RingInfo {
			std::int64_t chatId;
			ReminderInfo reminder;
			time_point<seconds> nextTp;
		};
		std::vector<RingInfo> ringNow;

		std::unique_lock lk(_m);
		_running = true;

		while (_running) {
			auto localTp = now();
			auto nextTpWakeUp = localTp + date::years(1);
			for (auto& [chatId, reminders] : _order) {
				for (auto it = reminders.begin(); it != reminders.end();) {
					if (it->first < localTp) {
						ringNow.push_back({chatId, it->second});
						auto& r = ringNow.back();
						it = reminders.erase(it);
						if (r.reminder.isRepeatable()) {
							r.nextTp = r.reminder.getNearTs(localTp);
							reminders.emplace(r.nextTp, r.reminder);
						}
					} else {
						if (it->first < nextTpWakeUp) {
							nextTpWakeUp = it->first;
						}
						break;
					}
				}
			}

			for (auto& r : ringNow) {
				std::string nextRing;
				if (r.reminder.isRepeatable()) {
					nextRing = fmt::format("\n\nСледующее напоминание:\n{}", prettyDateTime(r.nextTp));
				}
				_bot.getApi().sendMessage(r.chatId,
				    fmt::format("⏰{}⏰\n\n{}{}", r.reminder.descr, r.reminder.pretty(), nextRing));
			}
			ringNow.clear();

			_cond.wait_for(lk, nextTpWakeUp - now());
		}
	}

  private:
	mutable std::mutex _m;
	mutable std::condition_variable _cond;
	std::atomic_bool _running;

	Bot& _bot;
	std::unordered_map<std::int64_t /*chatId*/, std::multimap<time_point<seconds>, ReminderInfo>> _order;
};

std::vector<ReminderInfo> loadReminders(up::db& db, std::int64_t chatId) {
	auto collection = fmt::format("reminders_{}", chatId);
	up::value value = up::vm_fetch_all_records(db).fetch_value_or_throw(collection);

	if (!value.is_array() || value.size() == 0) {
		return {};
	}

	std::vector<ReminderInfo> res;
	value.foreach_array([&](int64_t i, const up::value& v) {
		auto id = v.at("__id").get_int_or_throw();
		ReminderInfo ri;
		ri.fromValue(v);
		res.push_back(ri);

		return true;
	});

	return res;
}

struct UserChat {
	std::int64_t userId;
	std::int64_t chatId;
};
std::vector<UserChat> loadUserChats(up::db& db) {
	up::value value = up::vm_fetch_all_records(db).fetch_value_or_throw("users");

	if (!value.is_array() || value.size() == 0) {
		return {};
	}

	std::vector<UserChat> res;
	value.foreach_array([&](int64_t i, const up::value& v) {
		res.push_back({v.at("id").get_int_or_throw(), v.at("chat_id").get_int_or_throw()});

		return true;
	});

	return res;
}

bool isChatRegistered(up::db& db, std::int64_t chatId) {
	auto collection = fmt::format("reminders_{}", chatId);
	return up::vm_collection_exist(db).exist(collection);
}

int main(int, char**) {
	signal(SIGINT, [](int s) {
		printf("SIGINT got\n");
		exit(0);
	});

	Bot bot(findToken());
	bot.getApi().deleteWebhook();

	up::db db("db.bin");

	ReminderQuery q(bot);

	auto start = [&](TgBot::Message::Ptr msg) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				return;
			}

			std::int64_t userId = msg->from->id;
			std::int64_t chatId = msg->chat->id;

			if (isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот уже существует в этом чате!");
				return;
			}

			auto collection = fmt::format("reminders_{}", chatId);
			db.compile_or_throw("db_create($col);").bind_or_throw("col", collection).exec_or_throw();
			db.commit_or_throw();

			up::vm_store_record(db).store_or_throw("users",
			    up::value::object{{"id", userId}, {"chat_id", msg->chat->id}});

			bot.getApi().sendMessage(msg->chat->id, "Здравствуйте, вы зарегестрированны.");
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	// auto addi = [&](TgBot::Message::Ptr msg) {
	// 	try {
	// 		if (!msg->chat) {
	// 			std::cerr << "Невозможно переслать сообщение" << std::endl;
	// 			return;
	// 		}

	// 		if (!msg->from) {
	// 			bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
	// 			return;
	// 		}

	// 		std::int64_t userId = msg->from->id;
	// 		std::int64_t chatId = msg->chat->id;

	// 		std::vector<std::string> args;
	// 		boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

	// 		if (args.size() == 1) {
	// 			bot.getApi().sendMessage(chatId, "Выберите дату и повтор:", false, 0, makeAddIKeyboard(now()));
	// 			bot.getApi().sendMessage(msg->chat->id, "qwe", false, 0, makeAddIKeyboard(now()));
	// 		}

	// 	} catch (const std::exception& e) { std::cerr << e.what(); }
	// };
	auto add = [&](TgBot::Message::Ptr msg) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				return;
			}

			std::int64_t userId = msg->from->id;
			std::int64_t chatId = msg->chat->id;

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				return;
			}

			std::string error;
			ReminderInfo ri;
			if (!ri.parseCommand(msg->text, error)) {
				bot.getApi().sendMessage(msg->chat->id, error);
				return;
			}

			if (ri.descr.size() > 200) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Сообщение должно быть меньше 200 символов utf-8!");
				return;
			}

			up::value v;
			ri.toValue(v);

			auto localTp = now();
			auto nextTp = ri.getNearTs(localTp);
			if (nextTp < localTp) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Напоминание уже прошло, так же оно не повоторяется!");
				return;
			}

			auto collection = fmt::format("reminders_{}", chatId);
			up::vm_store_record(db).store_or_throw(collection, v);
			q.addTimer(msg->chat->id, nextTp, ri);

			bot.getApi().sendMessage(msg->chat->id,
			    fmt::format("✅🗓️ Напоминание добавленно.\n{}\nСледующее срабатывание: {}", ri.pretty(),
			        prettyDateTime(nextTp)));
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	auto list = [&](TgBot::Message::Ptr msg) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				return;
			}

			std::int64_t userId = msg->from->id;
			std::int64_t chatId = msg->chat->id;

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				return;
			}

			std::vector<std::string> args;
			boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

			if (args.size() > 2) {
				bot.getApi().sendMessage(chatId, "⚠️ Неверное колличество аргументов!");
				return;
			}

			int page = 1;
			if (args.size() == 2)
				try {
					page = std::stoi(args.back());
				} catch (const std::exception& e) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат листов!");
					return;
				}

			auto collection = fmt::format("reminders_{}", chatId);
			up::value value;
			value = up::vm_fetch_all_records(db).fetch_value_or_throw(collection);

			std::string outMsg;
			if (!value.is_array() || value.size() == 0) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Еще нет напоминаний.");
				return;
			}
			auto start = std::max<int>(0, (page - 1) * 10);
			auto end = std::min<int>(value.size(), page * 10);

			for (int i = start; i != end; ++i) {
				ReminderInfo ri;
				ri.fromValue(value.at(i));
				outMsg += fmt::format("{}: {}\n", ri._id, ri.toString());
			}

			bot.getApi().sendMessage(msg->chat->id,
			    fmt::format("🗓️ Список напоминаний({}-{})/{}:\n{}", start, end, value.size(), outMsg));
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	auto del = [&](TgBot::Message::Ptr msg, bool allowMsgs) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				if (allowMsgs) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				}
				return;
			}

			auto userId = msg->from->id;
			auto chatId = msg->chat->id;

			if (!isChatRegistered(db, chatId)) {
				if (allowMsgs) {
					bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				}
				return;
			}

			std::vector<std::string> args;
			boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

			if (args.size() != 2) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат команды!");
				return;
			}
			std::int64_t recId;
			try {
				recId = std::stoi(args.back());
			} catch (const std::exception& e) {
				if (allowMsgs) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат id!");
				}
				return;
			}
			if (eraseReminder(db, chatId, recId)) {
				q.removeTimer(chatId, recId);
				if (allowMsgs) {
					bot.getApi().sendMessage(msg->chat->id, "✅ Напоминание удаленно.");
				}
			} else {
				if (allowMsgs) {
					bot.getApi().sendMessage(msg->chat->id, "❌ Напоминания не существует.");
				}
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	auto deli = [&](TgBot::Message::Ptr msg, std::int64_t messageId) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				return;
			}

			std::int64_t userId = msg->from->id;
			std::int64_t chatId = msg->chat->id;

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				return;
			}

			std::vector<std::string> args;
			boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });

			if (args.size() > 2) {
				bot.getApi().sendMessage(chatId, "⚠️ Неверное колличество аргументов!");
				return;
			}

			int page = 1;
			if (args.size() > 1)
				try {
					page = std::stoi(args[1]);
				} catch (const std::exception& e) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат листов!");
					return;
				}

			auto collection = fmt::format("reminders_{}", chatId);
			up::value value;
			value = up::vm_fetch_all_records(db).fetch_value_or_throw(collection);

			auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
			if (!value.is_array() || value.size() == 0) {
				if (messageId == 0) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Нет напоминаний.");
				} else {
					setButton(keyboard, 0, keyboard->inlineKeyboard.size(),
					    makeButon("Закрыть", fmt::format("/delete_last_msg")));
					bot.getApi().editMessageText("⚠️ Нет напоминаний.", chatId, messageId, "", "", false, keyboard);
				}

				return;
			}

			constexpr int PAGE_SIZE = 10;
			page = std::min<int>(page, (value.size() / PAGE_SIZE) + 1);

			auto start = std::max<int>(0, (page - 1) * PAGE_SIZE);
			auto end = std::min<int>(value.size(), page * PAGE_SIZE);

			for (int i = start; i != end; ++i) {
				ReminderInfo ri;
				ri.fromValue(value.at(i));
				setButton(keyboard, 0, i - start,
				    makeButon(fmt::format("{}", ri.toString()), fmt::format("/del {}", ri._id)));
			}
			if (end - start < value.size()) {
				if (start > 0 && end < value.size()) {
					setButton(keyboard, 0, end - start, makeButon("<", fmt::format("/deli {}", page - 1)));
					setButton(keyboard, 1, end - start, makeButon(">", fmt::format("/deli {}", page + 1)));
				} else if (start > 0) {
					setButton(keyboard, 0, end - start, makeButon("<", fmt::format("/deli {}", page - 1)));
				} else if (end < value.size()) {
					setButton(keyboard, 0, end - start, makeButon(">", fmt::format("/deli {}", page + 1)));
				}
			}
			setButton(keyboard, 0, keyboard->inlineKeyboard.size(),
			    makeButon("Отмена", fmt::format("/delete_last_msg")));

			if (messageId == 0) {
				bot.getApi().sendMessage(chatId, fmt::format("🗑️ Какое напоминание удалить❓"), false, 0, keyboard);
			} else {
				bot.getApi().editMessageText(fmt::format("🗑️ Какое напоминание удалить❓"), chatId, messageId, "", "",
				    false, keyboard);
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};

	bot.getEvents().onCommand("start", start);
	bot.getEvents().onCommand("list", list);
	bot.getEvents().onCommand("add", add);
	// bot.getEvents().onCommand("addi", addi);
	bot.getEvents().onCommand("del", [&](auto q) { del(q, true); });
	bot.getEvents().onCommand("deli", [&](auto q) { deli(q, 0); });

	bot.getEvents().onCallbackQuery([&](CallbackQuery::Ptr query) {
		query->message->text = query->data;

		std::vector<std::string> args;
		boost::split(args, query->data, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
		if (args.empty() || !query->message || !query->message->chat) {
			return;
		}
		if (args.front() == "/del") {
			del(query->message, false);
			deli(query->message, query->message->messageId);
		} else if (args.front() == "/deli") {
			deli(query->message, query->message->messageId);
		} else if (args.front() == "/delete_last_msg") {
			bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
		}
	});

	auto localTp = now();

	auto usersChats = loadUserChats(db);
	for (const auto& uc : usersChats) {
		auto rms = loadReminders(db, uc.chatId);
		for (const auto& r : rms) {
			auto nextTp = r.getNearTs(localTp);
			if (nextTp > localTp) {
				q.addTimer(uc.chatId, nextTp, r);
			}
		}
	}

	std::vector<BotCommand::Ptr> commands;
	BotCommand::Ptr cmdArray(new BotCommand);
	cmdArray->command = "start";
	cmdArray->description = "Регистрация пользователя";
	commands.push_back(cmdArray);

	cmdArray = BotCommand::Ptr(new BotCommand);
	cmdArray->command = "add";
	cmdArray->description = "Добавление нового напоминания. (Пр. /add 23.12.2023 14:30 w12345 Обед)";
	commands.push_back(cmdArray);

	cmdArray = BotCommand::Ptr(new BotCommand);
	cmdArray->command = "list";
	cmdArray->description = "Список напоминаний. /list [опц. номер листа]";
	commands.push_back(cmdArray);

	cmdArray = BotCommand::Ptr(new BotCommand);
	cmdArray->command = "del";
	cmdArray->description = "Удаление напоминания по id. /del [id]";
	commands.push_back(cmdArray);

	cmdArray = BotCommand::Ptr(new BotCommand);
	cmdArray->command = "deli";
	cmdArray->description = "Интерактивное удаление напоминания. /deli";
	commands.push_back(cmdArray);

	// cmdArray = BotCommand::Ptr(new BotCommand);
	// cmdArray->command = "help";
	// cmdArray->description = "Информация о формате команды /add.";
	// commands.push_back(cmdArray);

	bot.getApi().setMyCommands(commands);

	std::thread t([&q] { q.run(); });
	TgLongPoll longPoll(bot);
	printf("Start bot.\n");
	while (true) {
		try {
			longPoll.start();
		} catch (const std::exception& e) { printf("error: %s\n", e.what()); }
	}
	t.detach();
}
