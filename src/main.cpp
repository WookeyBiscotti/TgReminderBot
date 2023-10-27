#include "auto_reminder.hpp"
#include "reminder_info.hpp"
#include "utils.hpp"

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

using namespace std::chrono;
using namespace TgBot;

bool eraseReminder(up::db& db, std::int64_t chatId, std::int64_t recId) {
	auto vm = db.compile_or_throw("$result = db_drop_record($c, $recId);");
	std::string collection = fmt::format("reminders_{}", chatId);
	vm.bind_or_throw("c", collection);
	vm.bind_or_throw("recId", recId);
	vm.exec_or_throw();
	db.commit_or_throw();

	return vm.extract_or_throw("result").get_bool_or_throw();
}

std::pair<time_point_s, time_point_s> parseInfoArgs(std::vector<std::string>& args) {
	auto n = now();
	date::year_month_day ymd{date::sys_days{date::floor<date::days>(n.time_since_epoch())}};
	if (args.empty() || args[0] == "w") {
		auto d = (n - date::sys_days{ymd}.time_since_epoch()) + date::days(7);

		return {n, n + d.time_since_epoch()};
	} else if (args[0] == "cw") {
		auto cw = date::year_month_weekday(date::sys_days{ymd});
		auto d = date::sys_days{date::floor<date::days>(n.time_since_epoch()) - date::days(cw.weekday().c_encoding())};

		return {n, n + d.time_since_epoch()};
	} else if (args[0] == "m") {
		auto d = (n - date::sys_days{ymd}.time_since_epoch()) + date::days(31);

		return {n, n + d.time_since_epoch()};
	} else if (args[0] == "cm") {
		auto d = date::sys_days{date::floor<date::days>(date::sys_days{ymd + date::months(1)})};

		return {n, n + d.time_since_epoch()};
	} else {
		return {n, n};
	}
}

std::string prettyDateTime(time_point_s localTp) {
	date::year_month_day ymd{date::sys_days{date::floor<date::days>(localTp.time_since_epoch())}};
	date::time_of_day<minutes> tod{
	    date::floor<minutes>(localTp.time_since_epoch() - date::sys_days{ymd}.time_since_epoch())};

	return fmt::format("{:0>2}:{:0>2} {:0>2}/{:0>2}/{}", tod.hours().count(), tod.minutes().count(),
	    static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
}

std::string renderRemindersForInfo(const std::vector<std::pair<time_point_s, ReminderInfo>>& rems) {
	std::string out;

	if (rems.empty()) {
		return out;
	}

	for (auto& r : rems) {
		out += fmt::format("{}: {}\n", prettyDateTime(r.first), r.second.descr);
	}

	return out;
}

class ReminderQuery {
  public:
	ReminderQuery(Bot& bot): _bot(bot) {}
	void addTimer(std::int64_t chatId, time_point_s tp, const ReminderInfo& reminder) {
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

	std::vector<std::pair<time_point_s, ReminderInfo>> getInterval(std::int64_t chatId, time_point_s from,
	    time_point_s to) {
		std::unique_lock lk(_m);

		const auto& rems = _order[chatId];

		std::vector<std::pair<time_point_s, ReminderInfo>> out;
		for (auto& r : rems) {
			if (r.first <= from) {
				continue;
			}
			if (r.first > to) {
				break;
			}
			out.emplace_back(r.first, r.second);
		}

		return out;
	}

	void run() {
		struct RingInfo {
			std::int64_t chatId;
			ReminderInfo reminder;
			time_point_s nextTp;
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
	std::unordered_map<std::int64_t /*chatId*/, std::multimap<time_point_s, ReminderInfo>> _order;
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

	CurlHttpClient curlHttpClient;
	Bot bot(findToken(), curlHttpClient);
	bot.getApi().deleteWebhook();

	up::db db("db.bin");
	DynamicStorage ds(db, "dynamic_storage");

	ReminderQuery q(bot);

	auto start = [&](TgBot::Message::Ptr msg) {
		try {
			auto [userId, chatId] = getUserChatOrThrow(msg);

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
	auto add = [&](TgBot::Message::Ptr msg, CallbackQuery::Ptr query) {
		try {
			auto [userId, chatId] = getUserChatOrThrow(msg);

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				return;
			}

			std::string error;
			ReminderInfo ri;

			std::string argsStr;

			if (query) {
				argsStr = msg->text + query->data;
			} else {
				argsStr = msg->text;
			}

			if (!ri.parseCommand(argsStr, error)) {
				bot.getApi().sendMessage(chatId, error);
				return;
			}

			if (ri.descr.size() > 200) {
				bot.getApi().sendMessage(chatId, "⚠️ Сообщение должно быть меньше 200 байт!");
				return;
			}

			up::value v;
			ri.toValue(v);

			auto localTp = now();
			auto nextTp = ri.getNearTs(localTp);
			if (nextTp < localTp) {
				bot.getApi().sendMessage(chatId, "⚠️ Напоминание уже прошло, так же оно не повоторяется!");
				return;
			}

			auto collection = fmt::format("reminders_{}", chatId);
			ri._id = up::vm_store_record(db).store_or_throw(collection, v);

			q.addTimer(chatId, nextTp, ri);

			if (query) {
				bot.getApi().editMessageText(fmt::format("✅🗓️ Напоминание добавленно.\n{}\nСледующее срабатывание: {}",
				                                 ri.pretty(), prettyDateTime(nextTp)),
				    chatId, msg->messageId);
			} else {
				bot.getApi().sendMessage(chatId,
				    fmt::format("✅🗓️ Напоминание добавленно.\n{}\nСледующее срабатывание: {}", ri.pretty(),
				        prettyDateTime(nextTp)));
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	auto list = [&](TgBot::Message::Ptr msg) {
		try {
			auto [userId, chatId] = getUserChatOrThrow(msg);

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
	auto del = [&](TgBot::Message::Ptr msg, CallbackQuery::Ptr query) {
		try {
			auto [userId, chatId] = getUserChatOrThrow(msg);

			if (!isChatRegistered(db, chatId)) {
				if (!query) {
					bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				}
				return;
			}

			std::vector<std::string> args;
			if (query) {
				boost::split(args, query->data, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
			} else {
				boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
			}

			if (args.size() != 2) {
				bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат команды!");
				return;
			}
			std::int64_t recId;
			try {
				recId = std::stoi(args.back());
			} catch (const std::exception& e) {
				if (!query) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Неверный формат id!");
				}
				return;
			}
			if (eraseReminder(db, chatId, recId)) {
				q.removeTimer(chatId, recId);
				if (!query) {
					bot.getApi().sendMessage(msg->chat->id, "✅ Напоминание удаленно.");
				}
			} else {
				if (!query) {
					bot.getApi().sendMessage(msg->chat->id, "❌ Напоминания не существует.");
				}
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};
	auto deli = [&](TgBot::Message::Ptr msg, CallbackQuery::Ptr query) {
		try {
			auto [userId, chatId] = getUserChatOrThrow(msg);

			if (!isChatRegistered(db, chatId)) {
				bot.getApi().sendMessage(chatId, "⚠️ Бот еще не зарегестрирован в этом чате!(/start)");
				return;
			}

			std::vector<std::string> args;
			if (query) {
				boost::split(args, query->data, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
			} else {
				boost::split(args, msg->text, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
			}

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
				if (!query) {
					bot.getApi().sendMessage(msg->chat->id, "⚠️ Нет напоминаний.");
				} else {
					setButton(keyboard, 0, keyboard->inlineKeyboard.size(),
					    makeButon("Закрыть", fmt::format("/delete_me")));
					bot.getApi().editMessageText("⚠️ Нет напоминаний.", chatId, msg->messageId, "", "", false, keyboard);
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
			setButton(keyboard, 0, keyboard->inlineKeyboard.size(), makeButon("Отмена", fmt::format("/delete_me")));

			if (!query) {
				bot.getApi().sendMessage(chatId, fmt::format("🗑️ Какое напоминание удалить❓"), false, 0, keyboard);
			} else {
				bot.getApi().editMessageText(fmt::format("🗑️ Какое напоминание удалить❓"), chatId, msg->messageId, "",
				    "", false, keyboard);
			}
		} catch (const std::exception& e) { std::cerr << e.what(); }
	};

	bot.getEvents().onCommand("start", start);
	bot.getEvents().onCommand("list", list);
	// bot.getEvents().onCommand("add", [&](auto q) { add(q, nullptr); });
	bot.getEvents().onCommand("del", [&](auto q) { del(q, nullptr); });
	bot.getEvents().onCommand("deli", [&](auto q) { deli(q, 0); });

	bot.getEvents().onCallbackQuery([&](CallbackQuery::Ptr query) {
		std::vector<std::string> args;
		boost::split(args, query->data, [](char c) { return c == ' ' || c == '\n' || c == '\t'; });
		if (args.empty() || !query->message || !query->message->chat) {
			return;
		}
		if (args.front() == "/del") {
			del(query->message, query);
			deli(query->message, query);
		} else if (args.front() == "/deli") {
			deli(query->message, query);
		} else if (args.front() == "/delete_me") {
			bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
		} else if (args.front() == "/ar_date") {
			ar_date(bot, ds)(query);
		} else if (args.front() == "/ar_time") {
			ar_time(bot, ds)(query);
		} else if (args.front() == "/ar_repeat") {
			ar_repeat(bot, ds)(query);
		} else if (args.front() == "/add") {
			add(query->message, query);
		}
	});

	bot.getEvents().onAnyMessage([&](TgBot::Message::Ptr msg) {
		auto [userId, chatId] = getUserChatOrThrow(msg);

		if (msg->chat->type != Chat::Type::Private) {
			return;
		}
		if (msg->text[0] == '/') {
			return;
		}

		sendAutoReminderMsg(bot, msg->chat->id, msg->text);
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

	// cmdArray = BotCommand::Ptr(new BotCommand);
	// cmdArray->command = "add";
	// cmdArray->description = "Добавление нового напоминания. (Пр. /add 23.12.2023 14:30 w12345 Обед)";
	// commands.push_back(cmdArray);

	cmdArray = BotCommand::Ptr(new BotCommand);
	cmdArray->command = "list";
	cmdArray->description = "Список напоминаний. /list [опц. номер листа]";
	commands.push_back(cmdArray);

	// cmdArray = BotCommand::Ptr(new BotCommand);
	// cmdArray->command = "del";
	// cmdArray->description = "Удаление напоминания по id. /del [id]";
	// commands.push_back(cmdArray);

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
