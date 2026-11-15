"""Static SQL schema contract checks; not a real MySQL migration execution."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MIGRATION = (
    ROOT / "data" / "sql" / "db-characters"
    / "2026_11_15_00_playerbot_autonomy_enrollment.sql"
)


class EnrollmentSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sql = MIGRATION.read_text(encoding="utf-8")

    def test_two_idempotent_innodb_tables(self) -> None:
        tables = re.findall(r"CREATE TABLE IF NOT EXISTS\s+\x60([^\x60]+)\x60", self.sql)
        self.assertEqual(
            tables,
            ["playerbot_autonomy_enrollment", "playerbot_autonomy_enrollment_audit"],
        )
        self.assertEqual(self.sql.count("ENGINE=InnoDB"), 2)

    def test_guid_keys_state_and_audit_replay_keys(self) -> None:
        self.assertRegex(self.sql, r"\x60character_guid\x60\s+INT UNSIGNED NOT NULL")
        self.assertRegex(self.sql, r"PRIMARY KEY\s+\(\x60character_guid\x60\)")
        self.assertRegex(self.sql, r"\x60state\x60\s+ENUM\('PENDING', 'PERSISTENT', 'RETIRED'\)\s+NOT NULL")
        self.assertRegex(self.sql, r"\x60revision\x60\s+BIGINT UNSIGNED NOT NULL DEFAULT 1")
        self.assertRegex(self.sql, r"UNIQUE KEY\s+\x60uq_autonomy_enrollment_request\x60\s+\(\x60request_id\x60\)")
        self.assertRegex(self.sql, r"\x60request_id\x60\s+CHAR\(36\).*NOT NULL")
        self.assertRegex(self.sql, r"\x60event\x60\s+ENUM\('STAGE', 'CONFIRM', 'CANCEL', 'RETIRE', 'ORPHAN_RECONCILE'\)")
        self.assertIn(chr(96) + "actor_kind" + chr(96), self.sql)
        self.assertIn(chr(96) + "reason" + chr(96), self.sql)

    def test_migration_is_schema_only(self) -> None:
        statements = re.findall(
            r"(?im)^\s*(CREATE|INSERT|UPDATE|REPLACE|DELETE|DROP|ALTER|TRUNCATE)\s+",
            self.sql,
        )
        self.assertEqual([x.upper() for x in statements], ["CREATE", "CREATE"])
        self.assertNotRegex(self.sql, r"(?im)^\s*FOREIGN KEY\b")
        self.assertTrue(self.sql.rstrip().endswith(";"))

    def test_location_matches_target_module_db_updater(self) -> None:
        self.assertEqual(MIGRATION.parent.name, "db-characters")
        self.assertTrue(MIGRATION.name.startswith("2026_11_15_"))


if __name__ == "__main__":
    unittest.main()
