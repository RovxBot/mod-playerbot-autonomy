"""Production operation SQL on disposable MySQL; binary bindings tested separately."""
import re
import os
import unittest
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import test_enrollment_mysql as enrollment

ROOT = Path(__file__).resolve().parents[1]
SQL = dict(re.findall(r'char (\w+)\[\] = R"SQL\((.*?)\)SQL";',
                      (ROOT / 'src/Operations/OperationSql.h').read_text(), re.S))

@unittest.skipUnless(os.getenv('AUTONOMY_MYSQL_SOCKET') or os.getenv('AUTONOMY_MYSQL_HOST'),
                     'disposable MySQL not configured')
class OperationMySQLTests(unittest.TestCase):
    setUpClass = classmethod(enrollment.EnrollmentMySQLTests.setUpClass.__func__)
    tearDownClass = classmethod(enrollment.EnrollmentMySQLTests.tearDownClass.__func__)
    connect = classmethod(enrollment.EnrollmentMySQLTests.connect.__func__)
    row = enrollment.EnrollmentMySQLTests.row

    def setUp(self):
        enrollment.EnrollmentMySQLTests.setUp(self)
        with self.connection.cursor() as cur:
            for table in ('operation_journal', 'operation_lease', 'operation_member', 'operation'):
                cur.execute('DELETE FROM playerbot_autonomy_' + table)
            cur.execute('DELETE FROM playerbot_autonomy_execution WHERE operation_id<>0')
            cur.execute('CREATE TABLE IF NOT EXISTS guild (guildid INT UNSIGNED PRIMARY KEY, '
                        'leaderguid INT UNSIGNED, name VARCHAR(24)) ENGINE=InnoDB')
            cur.execute('DELETE FROM guild')
            cur.execute('DELETE FROM playerbot_autonomy_guild')
            cur.execute("INSERT INTO guild VALUES (23,1,'Ashen Vanguard')")
            cur.execute("INSERT INTO playerbot_autonomy_guild "
                        "(slot,founder_guid,account_id,faction,stage_revision,stage_request,name,guild_id,phase,reason) "
                        "VALUES(1,1,100,0,0,'00000001-0000-0001-1001-000000000000','Ashen Vanguard',23,'MANAGED','fixture')")
            cur.executemany('INSERT INTO characters(guid,account) VALUES(%s,100)', [(n,) for n in range(3,9)])
            cur.executemany('INSERT INTO guild_member VALUES(%s,23)', [(n,) for n in range(1,9)])
            cur.executemany("INSERT INTO playerbot_autonomy_enrollment(character_guid,account_id,state,revision) "
                            "VALUES(%s,100,'PERSISTENT',2)", [(n,) for n in range(1,9)])
        self.connection.commit()

    @staticmethod
    def op_execute(cur, name, args=()):
        return cur.execute(SQL[name].replace('?', '%s'), args)

    def propose(self, op, guids=range(1,6), connection=None):
        conn = connection or self.connection
        conn.begin()
        try:
            with conn.cursor() as cur:
                self.op_execute(cur, 'Insert', (op,0,23,1,0,0,'PROPOSED',100,100,190,220,0,'simulation'))
                self.op_execute(cur, 'Journal', (op,0,'PROPOSED','simulation'))
                self.op_execute(cur, 'Execution', (op,0,0,0,0,0,0))
                for pos,guid in enumerate(guids):
                    self.op_execute(cur, 'Member', (op,pos,guid,100,23,1 if pos==0 else 2 if pos==1 else 3))
            conn.commit()
        except self.driver.MySQLError:
            conn.rollback()
            raise

    def advance(self, op, rev, before, after, guids=range(1,6), connection=None, crash=False):
        conn = connection or self.connection
        conn.begin()
        try:
            with conn.cursor() as cur:
                self.op_execute(cur,'Change',(rev+1,after,101,rev+1,'test',op,rev,before))
                self.op_execute(cur,'Journal',(op,rev+1,after,'test'))
                self.op_execute(cur,'Execution',(op,rev+1,0,0,0,0,0))
                if after=='RESERVED':
                    for guid in guids:
                        self.op_execute(cur,'Lease',(guid,op,220,guid,100,23))
                if after in ('CANCELLED','FAILED','COMPLETED'):
                    self.op_execute(cur,'Release',(op,))
            if crash:
                conn.close()
            else:
                conn.commit()
        except self.driver.MySQLError:
            conn.rollback()
            raise

    def test_execution_receipts_migrate_once_and_fail_closed(self):
        self.propose(1)
        self.propose(2)
        migration = re.sub(r'(?m)^--.*$', '', (ROOT / 'data/sql/db-characters/'
            '2026_11_19_02_playerbot_autonomy_execution.sql').read_text())
        with self.connection.cursor() as cur:
            # Represent a pre-M3 database: neither receipts nor schema marker.
            cur.execute('DELETE FROM playerbot_autonomy_execution')
            for statement in migration.split(';'):
                if statement.strip(): cur.execute(statement)
            self.assertEqual(self.row('SELECT execution_mode,revision FROM playerbot_autonomy_execution '
                                      'WHERE operation_id=1'), (0,0))
            self.op_execute(cur,'Execution',(2,0,1,991,1,0,1))
            cur.execute('DELETE FROM playerbot_autonomy_execution WHERE operation_id=2')
            for statement in migration.split(';'):
                if statement.strip(): cur.execute(statement)
            self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_execution WHERE operation_id=2'),(0,))
            self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_execution WHERE operation_id=0'),(1,))
        self.connection.commit()

    def test_lease_conflict_atomic_and_cleanup(self):
        self.propose(1)
        self.propose(2, [6,7,8,1,2])
        self.advance(1,0,'PROPOSED','VALIDATED')
        self.advance(1,1,'VALIDATED','RESERVED')
        self.advance(2,0,'PROPOSED','VALIDATED')
        with self.assertRaises(self.driver.MySQLError):
            self.advance(2,1,'VALIDATED','RESERVED',[6,7,8,1,2])
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_operation_lease'), (5,))
        self.assertEqual(self.row('SELECT state,revision FROM playerbot_autonomy_operation WHERE operation_id=2'),
                         ('VALIDATED',1))
        self.advance(1,2,'RESERVED','CANCELLED')
        self.advance(2,1,'VALIDATED','RESERVED',[6,7,8,1,2])
        self.advance(2,2,'RESERVED','FAILED')
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_operation_lease'), (0,))

    def test_two_connections_reserve_one_party(self):
        for op in (1,2):
            self.propose(op)
            self.advance(op,0,'PROPOSED','VALIDATED')
        def attempt(op):
            conn = self.connect()
            try:
                self.advance(op,1,'VALIDATED','RESERVED',connection=conn)
                return True
            except self.driver.MySQLError:
                return False
            finally:
                conn.close()
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(attempt,(1,2)))
        self.assertEqual(sum(results), 1)
        self.connection.rollback()  # fresh REPEATABLE READ snapshot
        self.assertEqual(self.row('SELECT COUNT(DISTINCT operation_id),COUNT(*) '
                                  'FROM playerbot_autonomy_operation_lease'),(1,5))

    def test_revision_idempotency_crash_and_recovery(self):
        self.propose(1)
        with self.assertRaises(self.driver.MySQLError): self.propose(1)
        self.advance(1,0,'PROPOSED','VALIDATED')
        conn = self.connect()
        self.advance(1,1,'VALIDATED','RESERVED',connection=conn,crash=True)
        self.assertEqual(self.row('SELECT state,revision FROM playerbot_autonomy_operation'),('VALIDATED',1))
        self.connection.rollback()
        self.advance(1,1,'VALIDATED','RESERVED')
        # Lost acknowledgement: retry is rejected without an extra receipt;
        # the durable row identifies the successful original mutation.
        with self.assertRaises(self.driver.MySQLError): self.advance(1,1,'VALIDATED','RESERVED')
        self.advance(1,2,'RESERVED','RECOVERING')
        self.advance(1,3,'RECOVERING','CANCELLED')
        with self.assertRaises(self.driver.MySQLError): self.advance(1,3,'RECOVERING','CANCELLED')
        with self.connection.cursor() as cur:
            self.op_execute(cur,'Release',(1,))
            self.op_execute(cur,'Release',(1,))
        self.connection.commit()
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_operation_journal'),(5,))
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_operation_lease'),(0,))
        self.assertEqual(self.row('SELECT COUNT(*) FROM characters WHERE level<>41 OR money<>12345'),(0,))

    def test_m1_account_group_and_ownership_fail_closed(self):
        self.propose(1)
        self.advance(1,0,'PROPOSED','VALIDATED')
        for mutation, repair in (
            ("UPDATE playerbot_autonomy_enrollment SET state='RETIRED' WHERE character_guid=5",
             "UPDATE playerbot_autonomy_enrollment SET state='PERSISTENT' WHERE character_guid=5"),
            ('UPDATE characters SET account=999 WHERE guid=5', 'UPDATE characters SET account=100 WHERE guid=5'),
            ('INSERT INTO group_member(memberGuid) VALUES(5)', 'DELETE FROM group_member'),
            ("UPDATE playerbot_autonomy_guild SET phase='QUARANTINED'", "UPDATE playerbot_autonomy_guild SET phase='MANAGED'"),
        ):
            with self.connection.cursor() as cur: cur.execute(mutation)
            self.connection.commit()
            with self.assertRaises(self.driver.MySQLError): self.advance(1,1,'VALIDATED','RESERVED')
            self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_operation_lease'),(0,))
            with self.connection.cursor() as cur: cur.execute(repair)
            self.connection.commit()
