"""Runs production SQL text against a disposable MySQL 8 database only.

AUTONOMY_MYSQL_SOCKET and AUTONOMY_MYSQL_DATABASE must point at a throwaway DB.
No server is contacted by normal unittest discovery without that opt-in.
PyMySQL uses the text protocol here; tools/check_prepared_mysql.py separately
exercises binary prepared statements, production Store bindings and core Fields.
"""

from __future__ import annotations

import os
import re
import unittest
import uuid
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
SQL = dict(
    re.findall(r'char (\w+)\[\] = R"SQL\((.*?)\)SQL";', (ROOT / "src/Persistence/EnrollmentSql.h").read_text(), re.S)
)


@unittest.skipUnless(
    os.getenv("AUTONOMY_MYSQL_SOCKET") or os.getenv("AUTONOMY_MYSQL_HOST"), "disposable MySQL not configured"
)
class EnrollmentMySQLTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import pymysql

        cls.driver = pymysql
        cls.database = os.environ["AUTONOMY_MYSQL_DATABASE"]
        if not re.fullmatch(r"autonomy_test(?:_[a-z0-9]+)?", cls.database):
            raise ValueError("Only a disposable autonomy_test[_suffix] database is allowed")
        cls.connection = cls.connect()
        with cls.connection.cursor() as cur:
            cur.execute("SELECT VERSION()")
            version = cur.fetchone()[0]
            if not re.match(r"8\.", version):
                raise ValueError(f"MySQL 8 required, found {version}")
            print(f"\nDisposable MySQL {version}: {cls.database}")
            # Minimal engine-boundary fixtures, never copies of a private world DB.
            cur.execute("DROP TABLE IF EXISTS characters")
            cur.execute(
                "CREATE TABLE IF NOT EXISTS characters (guid INT UNSIGNED PRIMARY KEY, account INT UNSIGNED, deleteDate BIGINT NULL, online TINYINT DEFAULT 0, level INT DEFAULT 41, money INT DEFAULT 12345, name VARCHAR(12) DEFAULT 'Fixture', race TINYINT UNSIGNED DEFAULT 1) ENGINE=InnoDB"
            )
            cur.execute(
                "CREATE TABLE IF NOT EXISTS guild_member (guid INT UNSIGNED PRIMARY KEY, guildid INT UNSIGNED) ENGINE=InnoDB"
            )
            cur.execute("DROP TABLE IF EXISTS group_member")
            cur.execute("CREATE TABLE group_member (memberGuid INT UNSIGNED PRIMARY KEY, "
                        "guid INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB")
            cur.execute("CREATE TABLE IF NOT EXISTS `groups` (guid INT UNSIGNED PRIMARY KEY, "
                        "leaderGuid INT UNSIGNED NOT NULL, KEY (leaderGuid)) ENGINE=InnoDB")
            cur.execute("DELETE FROM `groups`")
            for _ in range(2):
                for path in sorted((ROOT / "data/sql/db-characters").glob("*.sql")):
                    sql = re.sub(r"(?m)^--.*$", "", path.read_text())
                    for statement in sql.split(";"):
                        if statement.strip():
                            cur.execute(statement)
        cls.connection.commit()

    @classmethod
    def connect(cls):
        return cls.driver.connect(
            unix_socket=os.getenv("AUTONOMY_MYSQL_SOCKET"),
            host=os.getenv("AUTONOMY_MYSQL_HOST", "localhost"),
            port=int(os.getenv("AUTONOMY_MYSQL_PORT", "3306")),
            user=os.getenv("AUTONOMY_MYSQL_USER", "root"),
            database=cls.database,
            autocommit=False,
            charset="utf8mb4",
        )

    @classmethod
    def tearDownClass(cls):
        cls.connection.close()

    def setUp(self):
        self.connection.rollback()
        with self.connection.cursor() as cur:
            for name in (
                "playerbot_autonomy_enrollment_audit",
                "playerbot_autonomy_transition_receipt",
                "playerbot_autonomy_enrollment",
                "guild_member",
                "group_member",
                "characters",
            ):
                cur.execute(f"DELETE FROM {name}")
            cur.executemany("INSERT INTO characters(guid, account) VALUES (%s, %s)", [(1, 100), (2, 100)])
        self.connection.commit()

    @staticmethod
    def execute(cur, name, args=()):
        # PyMySQL escapes/substitutes values into text queries. This checks SQL
        # semantics, not binary Field decoding or the production Store bindings.
        return cur.execute(SQL[name].replace("?", "%s"), args)

    def transition(
        self,
        event,
        before,
        revision,
        *,
        request=None,
        guid=1,
        account=100,
        guild=0,
        reason="test",
        connection=None,
        crash=False,
        fail_audit=False,
        actor="AUTHORIZED_ADMIN",
        actor_guid=0,
    ):
        connection = connection or self.connection
        request = request or str(uuid.uuid4())
        after = {"STAGE": "PENDING", "CONFIRM": "PERSISTENT", "CANCEL": "RETIRED", "RETIRE": "RETIRED"}[event]
        try:
            connection.begin()
            with connection.cursor() as cur:
                if before == "ABSENT":
                    self.execute(cur, "Stage", (guild, guid, account))
                else:
                    self.execute(
                        cur,
                        "Transition",
                        (
                            after,
                            event,
                            guild,
                            event,
                            event,
                            guid,
                            account,
                            revision,
                            before,
                            event,
                            guild,
                            event,
                            event,
                        ),
                    )
                self.execute(cur, "InsertReceipt", (request, revision, revision + 1))
                self.execute(
                    cur,
                    "Audit",
                    (request, guid, account, "INVALID" if fail_audit else event, before, after, guild, actor, actor_guid, reason),
                )
            if crash:
                connection.close()
            else:
                connection.commit()
        except self.driver.MySQLError:
            connection.rollback()
            raise
        return request

    def row(self, sql, args=()):
        with self.connection.cursor() as cur:
            cur.execute(sql, args)
            return cur.fetchone()

    def test_online_stage_cannot_race_maintenance(self):
        with self.connection.cursor() as cur:
            cur.execute("UPDATE characters SET online=1 WHERE guid=1")
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError):
            self.transition("STAGE", "ABSENT", 0)
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment"), (0,))

    def test_schema_and_empty_page_sentinel(self):
        with self.connection.cursor() as cur:
            self.execute(cur, "Schema")
            self.assertEqual(cur.fetchone(), (1,))
            self.execute(cur, "Page", (99, 128, 99, 128, 128))
            self.assertIsNone(cur.fetchone()[0])

    def test_pending_restart_confirm_transfer_disband(self):
        self.transition("STAGE", "ABSENT", 0)
        restarted = self.connect()
        with restarted.cursor() as cur:
            self.execute(cur, "Page", (0, 128, 0, 128, 128))
            self.assertEqual(cur.fetchone()[4:6], ("PENDING", 1))
        restarted.close()
        with self.connection.cursor() as cur:
            cur.execute("INSERT INTO guild_member VALUES(1, 7)")
        self.connection.commit()
        self.transition("CONFIRM", "PENDING", 1, guild=7)
        with self.connection.cursor() as cur:
            cur.execute("UPDATE guild_member SET guildid=8 WHERE guid=1")
            cur.execute("DELETE FROM guild_member WHERE guid=1")
        self.connection.commit()
        self.assertEqual(
            self.row("SELECT state, revision FROM playerbot_autonomy_enrollment WHERE character_guid=1"),
            ("PERSISTENT", 2),
        )
        self.assertEqual(self.row("SELECT level, money FROM characters WHERE guid=1"), (41, 12345))

    def test_stale_revision_and_duplicate_roll_back_atomically(self):
        request = self.transition("STAGE", "ABSENT", 0)
        for kwargs in ({"revision": 0}, {"revision": 1, "request": request}):
            with self.assertRaises(self.driver.MySQLError):
                self.transition("CANCEL", "PENDING", **kwargs)
        with self.assertRaises(self.driver.MySQLError):
            self.transition("STAGE", "ABSENT", 0, request=request, guid=2)
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment WHERE character_guid=2"), (0,))
        self.assertEqual(self.row("SELECT state,revision FROM playerbot_autonomy_enrollment"), ("PENDING", 1))
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment_audit"), (1,))
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_transition_receipt"), (1,))
        with self.connection.cursor() as cur:
            self.execute(cur, "Receipt", (request,))
            row = cur.fetchone()
        self.assertEqual(row[:5], (1, 100, "STAGE", "ABSENT", "PENDING"))
        self.assertEqual(row[9:], (0, 1))

    def test_audit_failure_rolls_back_enrollment_and_receipt(self):
        with self.assertRaises(self.driver.MySQLError):
            self.transition("STAGE", "ABSENT", 0, fail_audit=True)
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment"), (0,))
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_transition_receipt"), (0,))

    def test_interrupted_commit_and_retry(self):
        request = str(uuid.uuid4())
        self.transition("STAGE", "ABSENT", 0, request=request, connection=self.connect(), crash=True)
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment"), (0,))
        self.connection.commit()
        self.transition("STAGE", "ABSENT", 0, request=request)

    def test_safe_retirement_cancel_and_account_change(self):
        self.transition("STAGE", "ABSENT", 0)
        with self.connection.cursor() as cur:
            cur.execute("INSERT INTO guild_member VALUES(1, 7)")
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError):
            self.transition("CANCEL", "PENDING", 1)
        self.transition("CONFIRM", "PENDING", 1, guild=7)
        with self.assertRaises(self.driver.MySQLError):
            self.transition("RETIRE", "PERSISTENT", 2)
        with self.connection.cursor() as cur:
            cur.execute("DELETE FROM guild_member")
            cur.execute("INSERT INTO group_member(memberGuid) VALUES(1)")
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError):
            self.transition("RETIRE", "PERSISTENT", 2)
        with self.connection.cursor() as cur:
            cur.execute("DELETE FROM group_member")
            cur.execute("UPDATE characters SET account=101 WHERE guid=1")
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError):
            self.transition("RETIRE", "PERSISTENT", 2)
        with self.connection.cursor() as cur:
            cur.execute("UPDATE characters SET account=100 WHERE guid=1")
        self.connection.commit()
        self.transition("RETIRE", "PERSISTENT", 2)
        self.assertEqual(self.row("SELECT state,revision FROM playerbot_autonomy_enrollment"), ("RETIRED", 3))

    def test_orphan_reconcile_is_idempotent_and_retains_history(self):
        self.transition("STAGE", "ABSENT", 0)
        with self.connection.cursor() as cur:
            cur.execute("DELETE FROM characters WHERE guid=1")
            for _ in range(2):
                self.execute(cur, "Orphan", ("orphan-v1-1", 1, "orphan-v1-1"))
        self.connection.commit()
        self.assertEqual(self.row("SELECT COUNT(*) FROM playerbot_autonomy_enrollment_audit"), (2,))
        self.assertEqual(self.row("SELECT state FROM playerbot_autonomy_enrollment"), ("PENDING",))
        with self.connection.cursor() as cur:
            self.execute(cur, "Page", (0, 128, 0, 128, 128))
            row = cur.fetchone()
        self.assertEqual(row[0], 1)
        self.assertIsNone(row[1])

    def test_concurrent_revisions_have_one_winner(self):
        self.transition("STAGE", "ABSENT", 0)

        def attempt(_):
            connection = self.connect()
            try:
                self.transition("CANCEL", "PENDING", 1, connection=connection)
                return True
            except self.driver.MySQLError:
                return False
            finally:
                connection.close()

        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(attempt, range(2)))
        self.assertEqual(sorted(results), [False, True])
        self.assertEqual(self.row("SELECT revision FROM playerbot_autonomy_enrollment"), (2,))


if __name__ == "__main__":
    unittest.main()
