"""Disposable MySQL checks execute the exact production guild statements."""
import re
from pathlib import Path
import test_enrollment_mysql as enrollment
SQL=dict(re.findall(r'char (\w+)\[\] = R"SQL\((.*?)\)SQL";', (Path(__file__).resolve().parents[1]/'src/Guild/GuildSql.h').read_text(), re.S))
class GuildMySQLTests(enrollment.EnrollmentMySQLTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        with cls.connection.cursor() as cur:
            cur.execute('CREATE TABLE IF NOT EXISTS guild (guildid INT UNSIGNED PRIMARY KEY, leaderguid INT UNSIGNED, name VARCHAR(24)) ENGINE=InnoDB')
        cls.connection.commit()
    def setUp(self):
        super().setUp()
        with self.connection.cursor() as cur:
            cur.execute('DELETE FROM playerbot_autonomy_guild_audit')
            cur.execute('DELETE FROM playerbot_autonomy_guild')
            cur.execute('DELETE FROM guild')
        self.connection.commit()
    def guild_write(self,phase,*,before=None,guild=0,slot=1,guid=1,account=100,name='Ashen Vanguard',audit_phase=None):
        try:
            self.connection.begin()
            with self.connection.cursor() as cur:
                if before is None:
                    cur.execute(SQL['Insert'].replace('?','%s'),(slot,0,name,'test',0,'00000001-0000-0001-1001-000000000000',guid,account))
                else:
                    cur.execute(SQL['Change'].replace('?','%s'),(phase,guild,'test',slot,guid,account,before,phase,guild))
                cur.execute(SQL['Audit'].replace('?','%s'),(slot,audit_phase or phase,guild,'test'))
            self.connection.commit()
        except self.driver.MySQLError:
            self.connection.rollback()
            raise
    def test_intent_ownership_restart_and_duplicate(self):
        self.guild_write('PLANNED')
        self.guild_write('CREATING',before='PLANNED')
        with self.connection.cursor() as cur:
            cur.execute("INSERT INTO guild VALUES(7,1,'Ashen Vanguard')")
            cur.execute('INSERT INTO guild_member VALUES(1,7)')
        self.connection.commit()
        self.guild_write('MANAGED',before='CREATING',guild=7)
        with self.assertRaises(self.driver.MySQLError): self.guild_write('MANAGED',before='CREATING',guild=7)
        with self.connection.cursor() as cur:
            cur.execute(SQL['Load'])
            row=cur.fetchone()
        self.assertEqual(row[:6],(1,1,100,0,7,'MANAGED'))
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_guild_audit'),(3,))
        self.assertEqual(self.row('SELECT level,money FROM characters WHERE guid=1'),(41,12345))
    def test_conflict_does_not_adopt_or_erase_guild(self):
        self.guild_write('PLANNED')
        self.guild_write('CREATING',before='PLANNED')
        with self.connection.cursor() as cur:
            cur.execute("INSERT INTO guild VALUES(7,2,'Git Commit')")
            cur.execute('INSERT INTO guild_member VALUES(2,7)')
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError): self.guild_write('MANAGED',before='CREATING',guild=7)
        self.assertEqual(self.row('SELECT phase,guild_id FROM playerbot_autonomy_guild'),('CREATING',None))
        self.assertEqual(self.row('SELECT name,leaderguid FROM guild'),('Git Commit',2))
    def test_intent_failure_is_atomic_and_unique(self):
        with self.assertRaises(self.driver.MySQLError): self.guild_write('PLANNED',account=999)
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_guild'),(0,))
        with self.assertRaises(self.driver.MySQLError): self.guild_write('PLANNED',audit_phase='INVALID')
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_guild'),(0,))
        self.guild_write('PLANNED')
        with self.assertRaises(self.driver.MySQLError): self.guild_write('PLANNED',slot=2)
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_guild'),(1,))
    def test_quarantine_retains_pending_and_history(self):
        self.transition('STAGE','ABSENT',0)
        self.guild_write('PLANNED')
        self.guild_write('QUARANTINED',before='PLANNED')
        self.assertEqual(self.row('SELECT state FROM playerbot_autonomy_enrollment'),('PENDING',))
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_guild_audit'),(2,))

    def test_only_exact_director_stage_receipt_authorizes_handoff(self):
        request='00000001-0000-0001-1001-000000000000'
        self.guild_write('PLANNED')
        self.transition('STAGE','ABSENT',0,request=request,actor='RECRUITMENT',actor_guid=1,reason='director founding slot 1')
        with self.connection.cursor() as cur:
            cur.execute(SQL['Load'])
            self.assertEqual(cur.fetchone()[-1],1)
    def test_admin_pending_is_not_a_director_stage_receipt(self):
        request='00000001-0000-0001-1001-000000000000'
        self.guild_write('PLANNED')
        self.transition('STAGE','ABSENT',0,request=request,reason='director founding slot 1')
        with self.connection.cursor() as cur:
            cur.execute(SQL['Load'])
            self.assertEqual(cur.fetchone()[-1],0)
