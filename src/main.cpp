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

time_point<seconds> now() {
	static auto zone = date::locate_zone("Europe/Moscow");

	return time_point<seconds>(
	    duration_cast<seconds>(date::make_zoned(zone, system_clock::now()).get_local_time().time_since_epoch()));
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
	bool month_repeat = false;
	std::int64_t week_repeat = 0; // bit schema here
	std::int64_t day_repeat = 0;

	std::int64_t _id = -1;

	std::string pretty() const {
		std::string repeatInfo;
		if (year_repeat) {
			repeatInfo = "\nПовтор ежегодно.";
		} else if (month_repeat) {
			repeatInfo = "\nПовтор ежемесячно.";
		} else if (day_repeat) {
			repeatInfo = "\nПовтор ежедневно.";
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

		return fmt::format("{:0>2}/{:0>2}/{} {:0>2}:{:0>2} {}{}", day, month, year, hour, minute, descr, repeatInfo);
	}

	std::string toString() const {
		std::string repeatInfo;
		if (year_repeat) {
			repeatInfo = 'y';
		} else if (month_repeat) {
			repeatInfo = 'm';
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
		month_repeat = v.at("month_repeat").get_bool_or_throw();
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
				month_repeat = true;
			} else if (c == 'd') {
				try {
					day_repeat = std::stoi(args.front().substr(1));
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

		} else if (month_repeat) {
			if (remTp > currTp) {
				return remTp;
			}
			remDate = date::day(remDate.day()) / remDate.month() / currDate.year();
			remTp = toTp(remTime, remDate);

			while (remTp <= currTp || !remDate.ok()) {
				remDate += date::months(1);
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
	}

	void run() {
		struct RingInfo {
			std::int64_t chatId;
			ReminderInfo reminder;
			time_point<seconds> nextTp;
		};
		std::vector<RingInfo> ringNow;

		while (true) {
			auto localTp = now();

			for (auto& [chatId, reminders] : _order) {
				std::scoped_lock l(_m);
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
						break;
					}
				}
			}

			for (auto& r : ringNow) {
				std::string nextRing;
				if (r.reminder.isRepeatable()) {
					nextRing = fmt::format("\nСледующее напоминание:\n{}", prettyDateTime(r.nextTp));
				}
				_bot.getApi().sendMessage(r.chatId,
				    fmt::format("⏰ Напоминание ⏰\n*{}*{}", r.reminder.pretty(), nextRing), false, 0, nullptr,
				    "MarkdownV2");
			}
			ringNow.clear();

			std::this_thread::sleep_for(seconds(10));
		}
	}

  private:
	mutable std::mutex _m;

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

	bot.getEvents().onCommand("start", [&](TgBot::Message::Ptr msg) {
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

			up::vm_store_record(db).store_or_throw("users",
			    up::value::object{{"id", userId}, {"chat_id", msg->chat->id}});

			bot.getApi().sendMessage(msg->chat->id, "Здравствуйте, вы зарегестрированны.");
		} catch (const std::exception& e) { std::cerr << e.what(); }
	});

	bot.getEvents().onCommand("add", [&](TgBot::Message::Ptr msg) {
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
	});

	bot.getEvents().onCommand("list", [&](TgBot::Message::Ptr msg) {
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
				outMsg += fmt::format("*{}*: _{}_\n", ri._id, ri.toString());
			}

			bot.getApi().sendMessage(msg->chat->id,
			    fmt::format("🗓️ Список напоминаний({}-{})/{}:\n{}", start, end, value.size(), outMsg), false, 0, nullptr,
			    "MarkdownV2");
		} catch (const std::exception& e) { std::cerr << e.what(); }
	});

	bot.getEvents().onCommand("del", [&](TgBot::Message::Ptr msg) {
		try {
			if (!msg->chat) {
				std::cerr << "Невозможно переслать сообщение" << std::endl;
				return;
			}

			if (!msg->from) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Несуществующий пользователь!");
				return;
			}

			auto userId = msg->from->id;
			auto chatId = msg->chat->id;

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
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
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат id!");
				return;
			}
			if (eraseReminder(db, chatId, recId)) {
				q.removeTimer(chatId, recId);
				bot.getApi().sendMessage(msg->chat->id, "✅ Напоминание удаленно.");
			} else {
				bot.getApi().sendMessage(msg->chat->id, "❌ Напоминания не существует.");
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
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

	// cmdArray = BotCommand::Ptr(new BotCommand);
	// cmdArray->command = "help";
	// cmdArray->description = "Информация о формате команды /add.";
	// commands.push_back(cmdArray);

	bot.getApi().setMyCommands(commands);

	std::thread t([&q] { q.run(); });
	TgLongPoll longPoll(bot);
	while (true) {
		try {
			printf("Long poll started\n");
			longPoll.start();
		} catch (const std::exception& e) { printf("error: %s\n", e.what()); }
	}
	t.detach();
}
