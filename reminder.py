import datetime
import re

from calendar import monthrange

date_re = re.compile("(?P<day>\d{1,2}|\*)\.(?P<month>\d{1,2}|\*).(?P<year>\d{2,2}(?:\d\d)?|\*|0)")
time_re = re.compile("(?P<hours>\d{1,2}|\*)\:(?P<minutes>\d{1,2}|\*)(?:\:(?P<seconds>\d{1,2}))?")


def number(str_num):
    try:
        return int(str_num)
    finally:
        return None


class Reminder:
    def __init__(self, chat_id: str, time_zone: datetime.timezone, time_form):
        self.time_form = time_form
        self.chat_id = chat_id
        self.timezone = time_zone
        if len(time_form) < 3:
            raise RuntimeError(
                "Current args: {}, len {}. Usage /add [date] [time] [name]".format(time_form, len(time_form)))

        idx = 0
        res, err = self._get_date(time_zone, time_form[idx])
        if not res: raise RuntimeError(err)
        idx += 1

        res, err = self._get_time(time_form[idx])
        if not res: raise RuntimeError(err)
        idx += 1

        self.name = " ".join([time_form[i] for i in range(idx, len(time_form))])

        self.near_ts = self._get_near_ts()

        if self.near_ts == 0:
            raise RuntimeError("User max date is lower then current: {}".format(datetime.datetime.now()))

    def print(self):
        def a(num) -> str:
            return "*" if num == -1 else num

        return "Name: {}, time: {}:{}:{}, date: {}.{}.{}, near: {}".format(self.name,
                                                                           a(self.hour), a(self.minute), a(self.second),
                                                                           a(self.day), a(self.month), a(self.year),
                                                                           datetime.datetime.fromtimestamp(
                                                                               self.near_ts).strftime(
                                                                               "%H:%M:%S %m.%d.%Y"))

    def id(self):
        return str(self.chat_id) + " ".join(self.time_form)

    def _get_time(self, time_str: str) -> (bool, str):
        num = number(time_str)
        if num is not None:
            if 0 <= num < 24:
                self.hour = num
                self.minute = 0
                self.second = 0

                return True, ""
            else:
                return False, "Wrong hours number. Must be 0 <= hours < 24. Actually: {}".format(num)

        m = re.match(time_re, time_str)
        if m is not None:
            self.hour = m.groupdict().get("hours")
            self.minute = m.groupdict().get("minutes")
            self.second = m.groupdict().get("seconds")
            if self.second is None:
                self.second = "0"

            if self.hour == "*":
                self.hour = -1
            else:
                self.hour = int(self.hour)
                if self.hour > 24:
                    return False, "Wrong hours number. Must be 0 <= hours <= 24. Actually: {}".format(self.hour)

            if self.minute == "*":
                self.minute = -1
            else:
                self.minute = int(self.minute)
                if self.minute > 59:
                    return False, "Wrong minutes number. Must be 0 <= minutes <= 59. Actually: {}".format(self.minute)

            self.second = int(self.second)
            if self.second > 59:
                return False, "Wrong seconds number. Must be 0 <= seconds <= 59. Actually: {}".format(self.second)
        else:
            return False, "time format 19:00 or 17:* or *:00 or 01:02:03"

        return True, ""

    def _get_date(self, time_zone: datetime.timezone, date_str: str) -> (bool, str):
        num = number(date_str)
        now = datetime.datetime.now(time_zone)
        if num is not None:
            if num <= monthrange(now.date().year, now.date().month)[1]:
                self.day = num
                self.month = now.date().month
                self.year = now.date().year

                return True, ""
            else:
                return False, "Wrong day number. Must be 0 < day <= {}. Actually: {}".format(
                    monthrange(now.date().year, now.date().month)[1], num)

        m = re.match(date_re, date_str)
        if m is not None:
            self.day = m.groupdict().get("day")
            self.month = m.groupdict().get("month")
            self.year = m.groupdict().get("year")

            if self.year == "*":
                self.year = -1
            else:
                self.year = int(self.year)
                if self.year < now.date().year:
                    return False, "Wrong year number. Must be {} <= year. Actually: {}".format(now.date().year,
                                                                                               self.year)

            if self.month == "*":
                self.month = -1
            else:
                self.month = int(self.month)
                if self.month > 12:
                    return False, "Wrong month number. Must be 1 <= month <= 12. Actually: {}".format(self.month)
                elif self.year == now.date().year and self.month < now.date().month:
                    return False, "Wrong month number. Must be {} <= month <= 12. Actually: {}".format(now.date().month,
                                                                                                       self.month)

            if self.day == "*":
                self.day = -1
            else:
                self.day = int(self.day)
                days_in_month = 31 if self.year == -1 or self.month == -1 else monthrange(self.year, self.month)[1]
                if self.day > days_in_month:
                    return False, "Wrong seconds number. Must be 0 < day <= {}. Actually: {}".format(days_in_month,
                                                                                                     self.second)
                elif self.day == now.date().year and self.month == now.date().month and self.day < now.date().day:
                    return False, "Wrong day number. Must be {} <= day <= {}. Actually: {}".format(now.date().day,
                                                                                                   days_in_month,
                                                                                                   self.second)
        else:
            return False, "date format 1.2.1991 or *.12.2021"

        return True, ""

    def update_near_ts(self):
        self.near_ts = self._get_near_ts()

    def _get_near_ts(self):
        now = datetime.datetime.now(self.timezone)
        now_ts = now.timestamp()

        near = datetime.datetime(self.year, 1, 1, tzinfo=self.timezone) if self.year != -1 else datetime.datetime(
            now.year, 1, 1, tzinfo=self.timezone)

        near = near.replace(second=59 if self.second == -1 else self.second)
        near = near.replace(minute=59 if self.minute == -1 else self.minute)
        near = near.replace(hour=23 if self.hour == -1 else self.hour)
        near = near.replace(month=12 if self.month == -1 else self.month)
        near = near.replace(day=monthrange(now.year, near.month)[1] if self.day == -1 else self.day)

        if self.year == -1:
            if near.timestamp() < now_ts:
                near = near.replace(year=near.year + 1)
        else:
            if near.timestamp() < now_ts:
                return 0

        if self.month == -1:
            while near.month > 1:
                new_near = near.replace(month=near.month - 1, day=monthrange(now.year, near.month - 1)[1])
                if new_near.timestamp() < now_ts:
                    break
                near = new_near

        if self.day == -1:
            while near.day > 1:
                new_near = near.replace(day=near.day - 1)
                if new_near.timestamp() < now_ts:
                    break
                near = new_near

        if self.hour == -1:
            while near.hour > 0:
                new_near = near.replace(hour=near.hour - 1)
                if new_near.timestamp() < now_ts:
                    break
                near = new_near

        if self.minute == -1:
            while near.minute > 0:
                new_near = near.replace(minute=near.minute - 1)
                if new_near.timestamp() < now_ts:
                    break
                near = new_near

        if self.second == -1:
            while near.second > 0:
                new_near = near.replace(second=near.second - 1)
                if new_near.timestamp() < now_ts:
                    break
                near = new_near

        return near.timestamp()

    def save(self, connect):
        pass
