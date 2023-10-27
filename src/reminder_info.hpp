#pragma once

#include "auto_reminder.hpp"
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

	std::int64_t pre_reminder = 0;

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

		if (auto pr = v.find("pre_reminder")) {
			pre_reminder = pr->get_int_or_throw();
		}

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

		v["pre_reminder"] = pre_reminder;
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
		for (const auto& w : args) {
			descr += w;
			if (&w != &args.back()) {
				descr += ' ';
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

	time_point_s getNearTs(time_point_s localTp) const {
		using namespace std::chrono;

		auto toTp = [](const date::time_of_day<minutes>& t, const date::year_month_day& d) {
			return time_point_s{duration_cast<seconds>(t.to_duration() + date::sys_days{d}.time_since_epoch())};
		};

		time_point_s res;

		if (!on) {
			return time_point_s(minutes(0));
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
			auto weekIndex = [](const time_point_s& tp) {
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
