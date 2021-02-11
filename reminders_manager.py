import sqlite3
import datetime

from reminder import Reminder
from migration import Migration
from threading import get_ident
from telegram.ext import CallbackContext

MIGRATION_DIR = "migration"
DB_PATH = "reminders.db"


def remind(context: CallbackContext):
    r: Reminder = context.job.context[1]
    m: RemindersManager = context.job.context[0]
    r.update_near_ts()
    msg = "📆 {}".format(r.name)
    if r.near_ts != 0:
        msg += " next: {}".format(datetime.datetime.fromtimestamp(r.near_ts).strftime("%H:%M:%S %m.%d.%Y"))

    context.bot.send_message(r.chat_id, text=msg)

    if r.near_ts != 0 and datetime.datetime.now().replace(hour=23, minute=59,
                                                          second=59).timestamp() > r.near_ts:
        context.job_queue.run_once(remind, 1 + r.near_ts - datetime.datetime.now().timestamp(), context=r,
                                   name=r.id())

    m.update_reminder(r)


class RemindersManager:
    def __init__(self):
        Migration(DB_PATH, MIGRATION_DIR)
        self.connection_data = DB_PATH
        self.connections = {}

    def add_user(self, chat_id: str, time_zone: datetime.timezone):
        # TODO: optimize
        cursor = self.db_connection().cursor()
        cursor.execute('''INSERT INTO Users VALUES({},{})'''.format(chat_id, time_zone.tzname(None)))
        self.db_connection().commit()
        cursor.close()

    def db_connection(self):
        if get_ident() not in self.connections:
            connection = sqlite3.connect(self.connection_data)
            self.connections[get_ident()] = connection
            return connection

        return self.connections[get_ident()]

    def update_reminder(self, r: Reminder):
        # TODO: optimize
        if r.near_ts == 0:
            cursor = self.db_connection().cursor()
            cursor.execute(
                '''DELETE FROM Reminder WHERE chat_id = "{}" and time_form = "{}"'''.format(r.chat_id, r.time_form))
            self.db_connection().commit()
            cursor.close()
        else:
            cursor = self.db_connection().cursor()
            cursor.execute(
                '''UPDATE Reminder SET near_ts={} WHERE chat_id = "{}" and time_form = "{}"'''.format(
                    r.near_ts, r.chat_id, r.time_form))
            self.db_connection().commit()
            cursor.close()

    def add_reminder(self, r: Reminder, context: CallbackContext):
        # TODO: optimize
        cursor = self.db_connection().cursor()
        query = '''INSERT INTO Reminder VALUES({},"{}","{}","{}","{}",{},{},{},{},{},{})'''.format(r.near_ts, r.chat_id,
                                                                                                   r.timezone.tzname(
                                                                                                       None),
                                                                                                   r.time_form, r.name,
                                                                                                   r.hour,
                                                                                                   r.minute, r.second,
                                                                                                   r.day, r.month,
                                                                                                   r.year)

        cursor.execute(query)
        self.db_connection().commit()
        cursor.close()

        if datetime.datetime.now().replace(hour=23, minute=59, second=59).timestamp() > r.near_ts:
            context.job_queue.run_once(remind,
                                       1 + r.near_ts - datetime.datetime.now().timestamp(), context=(self, r),
                                       name=r.id())

    def daily_update(self, context: CallbackContext):
        # TODO: optimize
        cursor = self.db_connection().cursor()
        cursor.execute('''SELECT chat_id, tzinfo, time_form FROM Reminder WHERE near_ts < {}'''.format(
            (datetime.datetime.now() + datetime.timedelta(days=1)).timestamp()))

        LOCAL_TIMEZONE = datetime.datetime.now(datetime.timezone.utc).astimezone().tzinfo
        for row in cursor.fetchall():
            try:
                r = Reminder(chat_id=row[0], time_zone=LOCAL_TIMEZONE, time_form=eval(row[2]))
                context.job_queue.run_once(remind,
                                           1 + r.near_ts - datetime.datetime.now().timestamp(), context=(self, r),
                                           name=r.id())
            except RuntimeError as e:
                cursor.execute(
                    '''DELETE FROM Reminder WHERE chat_id = "{}" and time_form = "{}"'''.format(row[0], row[2]))
                self.db_connection().commit()
        cursor.close()

    def get_all_reminders(self):
        reminders = []
        cursor = self.db_connection().cursor()
        cursor.execute('''SELECT chat_id, tzinfo, time_form FROM Reminder''')
        LOCAL_TIMEZONE = datetime.datetime.now(datetime.timezone.utc).astimezone().tzinfo
        for row in cursor.fetchall():
            try:
                reminders.append(Reminder(chat_id=row[0], time_zone=LOCAL_TIMEZONE, time_form=eval(row[2])))
            except RuntimeError as e:
                cursor.execute(
                    '''DELETE FROM Reminder WHERE chat_id = "{}" and time_form = "{}"'''.format(row[0], row[2]))
                self.db_connection().commit()
        cursor.close()

        return reminders
