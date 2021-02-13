#!/usr/bin/env python
# pylint: disable=W0613, C0116
# type: ignore[union-attr]
# This program is dedicated to the public domain under the CC0 license.

"""
Simple Bot to send timed Telegram messages.

This Bot uses the Updater class to handle the bot and the JobQueue to send
timed messages.

First, a few handler functions are defined. Then, those functions are passed to
the Dispatcher and registered at their respective places.
Then, the bot is started and runs until we press Ctrl-C on the command line.

Usage:
Basic Alarm Bot example, sends a message after a set time.
Press Ctrl-C on the command line or send a signal to the process to stop the
bot.
"""

import logging
import reminder
import datetime
import pytz
import re

from telegram import Update
from telegram.ext import (
    Updater,
    CommandHandler,
    MessageHandler,
    Filters,
    ConversationHandler,
    CallbackContext,
)

from reminders_manager import RemindersManager

# Enable logging
logging.basicConfig(
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.INFO
)

logger = logging.getLogger(__name__)

manager = RemindersManager()


def start_begin(update: Update, context: CallbackContext) -> int:
    update.message.reply_text('Hi!')
    return 0


def start_body(update: Update, context: CallbackContext) -> int:
    update.message.reply_text('Enter yor current time for timezone identification: e.g. 19:33')
    return 1


def start_end(update: Update, context: CallbackContext) -> int:
    text = update.message.text
    now = datetime.datetime.now(tz=pytz.FixedOffset(0))
    m = re.match("(?P<hour>[0-9]{1,2})\:(?P<minute>[0-9]{1,2})", update.message.text)
    hour = int(m.groupdict().get("hour"))
    minute = int(m.groupdict().get("minute"))
    if 23 < hour or hour < 0 or minute > 59 or minute < 0:
        return 0

    dt = now.replace(hour=hour, minute=minute) - now

    hour = dt.total_seconds() // 3600
    if (abs(dt.total_seconds()) / 60) % 60 > 30:
        hour = hour + (-1 if dt.total_seconds() < 0 else 1)

    if abs(hour) >= 12:
        hour = hour + (24 if hour < 0 else -24)

    manager.add_chat(update.message.chat_id, hour)

    update.message.reply_text("Chat added")

    return ConversationHandler.END


def add_reminder(update: Update, context: CallbackContext) -> None:
    try:
        c = manager.get_chat(update.message.chat_id)
        r = reminder.Reminder(update.message.chat_id, c.utc, context.args)
        manager.add_reminder(r, context)
    except RuntimeError as e:
        update.message.reply_text(str(e))
        return

    update.message.reply_text(r.print())


def remove_reminder_start(update: Update, context: CallbackContext) -> int:
    reminders_list(update, context)
    return 0


def remove_job_if_exists(name, context):
    """Remove job with given name. Returns whether job was removed."""
    current_jobs = context.job_queue.get_jobs_by_name(name)
    if not current_jobs:
        return False
    for job in current_jobs:
        job.schedule_removal()
    return True


def remove_reminder_body(update: Update, context: CallbackContext) -> int:
    text = update.message.text
    reminders = context.user_data["reminders"]

    try:
        r = reminders[int(text)]
        r.near_ts = 0
        manager.update_reminder(r)
        remove_job_if_exists(r.id(), context)
        update.message.reply_text("Reminder removed")
    except Exception as e:
        update.message.reply_text(str(e))

    return ConversationHandler.END


def remove_reminder_end(update: Update, context: CallbackContext) -> int:
    return ConversationHandler.END


def reminders_list(update: Update, context: CallbackContext) -> None:
    reminders = manager.get_all_reminders()
    i = 0
    msg = ""
    for r in reminders:
        msg += "{}: {}\n\n".format(i, r.print())
        i += 1

    update.message.reply_text(msg)
    context.user_data["reminders"] = reminders


def main():
    """Run bot."""
    # Create the Updater and pass it your bot's token.
    with open("telegram_private_key", "r") as key:
        updater = Updater(key.read())

    # Get the dispatcher to register handlers
    dispatcher = updater.dispatcher

    # on different commands - answer in Telegram
    # dispatcher.add_handler(CommandHandler("start", start))
    # dispatcher.add_handler(CommandHandler("help", start))
    dispatcher.add_handler(CommandHandler("add", add_reminder))
    dispatcher.add_handler(CommandHandler("list", reminders_list))

    del_handler = ConversationHandler(
        entry_points=[CommandHandler('del', remove_reminder_start)],
        states={
            0: [
                MessageHandler(
                    None, remove_reminder_body
                )
            ]
        },
        fallbacks=[MessageHandler(
            None,
            remove_reminder_end
        )]
    )
    dispatcher.add_handler(del_handler)

    start_handler = ConversationHandler(
        entry_points=[CommandHandler('start', start_begin)],
        states={
            0: [
                MessageHandler(
                    None, start_body
                )
            ],
            1: [
                MessageHandler(
                    None, start_end
                )
            ]
        },
        fallbacks=[MessageHandler(
            None,
            start_body
        )]
    )
    dispatcher.add_handler(start_handler)

    # Start the Bot
    updater.start_polling()

    def daily_update(c):
        context: CallbackContext = c[0]
        m: RemindersManager = c[1]
        m.daily_update(context)

    manager.daily_update(updater)

    updater.job_queue.run_repeating(daily_update, 24 * 60 * 60, context=(updater, manager), name="daily_updater")

    # Block until you press Ctrl-C or the process receives SIGINT, SIGTERM or
    # SIGABRT. This should be used most of the time, since start_polling() is
    # non-blocking and will stop the bot gracefully.
    updater.idle()


if __name__ == '__main__':
    main()
