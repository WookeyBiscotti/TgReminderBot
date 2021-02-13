import sqlite3
import datetime
from os import listdir
from os.path import isfile, join


def is_table_exist(cursor: sqlite3.Cursor, table_name: str) -> bool:
    cursor.execute(''' SELECT count(name) FROM sqlite_master WHERE type='table' AND name='{}' '''.format(table_name))
    return cursor.fetchone()[0] != 0


def is_id_exist(cursor: sqlite3.Cursor, table_name: str, key_name: str, value) -> bool:
    cursor.execute(
        ''' SELECT count({key_name}) FROM {table_name} WHERE {key_name}='{value}' '''.format(table_name=table_name,
                                                                                             key_name=key_name,
                                                                                             value=value))
    return cursor.fetchone()[0] != 0


class Migration:
    def __init__(self, connection_data: str, migrations_path: str):
        self.connection_data = connection_data
        self.migrations_path = migrations_path
        self.connection = sqlite3.connect(connection_data)

        self._create_base_tables()
        self._apply_migrations()

    def _create_base_tables(self):
        cursor = self.connection.cursor()

        if not is_table_exist(cursor, "migration"):
            cursor.execute("CREATE TABLE migration (current_migration_id INT, comment TEXT)")

        if not is_table_exist(cursor, "migration_history"):
            cursor.execute("CREATE TABLE migration_history (INT ts, migration_id INT, comment TEXT)")

        self.connection.commit()

    def _apply_migrations(self):
        def sort_nicely(file_list):
            def alphanum_key(name):
                def try_int(file_part):
                    try:
                        return int(file_part)
                    finally:
                        return file_part

                import re
                return [try_int(c) for c in re.split('([0-9]+)', name)]

            file_list.sort(key=alphanum_key)

        files = [f for f in listdir(self.migrations_path) if
                 isfile(join(self.migrations_path, f)) and f.endswith(".sql")]

        sort_nicely(files)

        cursor = self.connection.cursor()
        for file_name in files:
            if not is_id_exist(cursor, "migration_history", "migration_id", file_name.replace(".sql", "")):
                with open(join(self.migrations_path, file_name), 'r') as file:
                    print(f"Migration {file_name}")
                    cursor.execute(file.read())
                    self.connection.commit()

                cursor.execute(
                    ''' INSERT INTO migration_history VALUES({ts},{id},"") '''.format(
                        ts=datetime.datetime.now().timestamp(),
                        id=file_name.replace(".sql", "")))
                self.connection.commit()


        cursor.close()
