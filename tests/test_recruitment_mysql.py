"""Production recruitment SQL on disposable fixtures; guild API is a simulated boundary."""
import re
from pathlib import Path
import test_guild_mysql as guilds
SQL=dict(re.findall(r'char (\w+)\[\] = R"SQL\((.*?)\)SQL";', (Path(__file__).resolve().parents[1]/'src/Guild/RecruitmentSql.h').read_text(), re.S))
class RecruitmentMySQLTests(guilds.GuildMySQLTests):
    def setUp(self):
        super().setUp()
        with self.connection.cursor() as cur:
            cur.execute('DELETE FROM playerbot_autonomy_recruitment_audit')
            cur.execute('DELETE FROM playerbot_autonomy_recruitment')
        self.connection.commit()
    def managed(self):
        self.guild_write('PLANNED')
        self.transition('STAGE','ABSENT',0,request='00000001-0000-0001-1001-000000000000',actor='RECRUITMENT',actor_guid=1,reason='director founding slot 1')
        self.guild_write('CREATING',before='PLANNED')
        with self.connection.cursor() as cur:
            cur.execute("INSERT INTO guild VALUES(7,1,'Ashen Vanguard')")
            cur.execute('INSERT INTO guild_member VALUES(1,7)')
        self.connection.commit()
        self.guild_write('MANAGED',before='CREATING',guild=7)
        self.transition('CONFIRM','PENDING',1,guild=7)
    def recruit_write(self,phase,*,before=None,actor=1,fail_audit=False):
        try:
            self.connection.begin()
            with self.connection.cursor() as cur:
                if before is None:
                    cur.execute(SQL['Insert'].replace('?','%s'),(7,1,actor,0,'00000002-0000-0007-2001-000000000000','test',1,7,2,100))
                else:
                    cur.execute(SQL['Change'].replace('?','%s'),(phase,'test',2,100,7,actor,before,phase))
                cur.execute(SQL['Audit'].replace('?','%s'),(2,'INVALID' if fail_audit else phase,7,actor,'test'))
            self.connection.commit()
        except self.driver.MySQLError:
            self.connection.rollback()
            raise
    def recruit_stage(self,**kwargs):
        self.transition('STAGE','ABSENT',0,guid=2,guild=kwargs.get('guild',7),request='00000002-0000-0007-2001-000000000000',actor='RECRUITMENT',actor_guid=kwargs.get('actor',1),reason='director recruitment guild 7')
    def test_recruitment_restart_after_add_then_confirm(self):
        self.managed(); self.recruit_write('SELECTED'); self.recruit_stage(); self.recruit_write('JOINING',before='SELECTED')
        with self.connection.cursor() as cur: cur.execute('INSERT INTO guild_member VALUES(2,7)')
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('COMPLETE',before='JOINING')
        restarted=self.connect()
        with restarted.cursor() as cur:
            cur.execute(SQL['Load'].replace('?','%s'),(0,))
            row=cur.fetchone()
        restarted.close()
        self.assertEqual((row[0],row[2],row[7],row[-1]),(2,7,'JOINING',1))
        self.transition('CONFIRM','PENDING',1,guid=2,guild=7,actor='RECRUITMENT',actor_guid=1)
        self.recruit_write('COMPLETE',before='JOINING')
        self.assertEqual(self.row('SELECT COUNT(*) FROM guild_member WHERE guid=2'),(1,))
        self.assertEqual(self.row('SELECT level,money FROM characters WHERE guid=2'),(41,12345))
    def test_recruitment_duplicate_and_actor_cas(self):
        self.managed(); self.recruit_write('SELECTED')
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('SELECTED')
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('JOINING',before='SELECTED',actor=2)
        self.recruit_write('JOINING',before='SELECTED')
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('JOINING',before='SELECTED')
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_recruitment_audit'),(2,))
    def test_wrong_actor_receipt_never_proves_director_stage(self):
        self.managed(); self.recruit_write('SELECTED'); self.recruit_stage(actor=2)
        with self.connection.cursor() as cur:
            cur.execute(SQL['Load'].replace('?','%s'),(0,))
            self.assertEqual(cur.fetchone()[-1],0)
        self.recruit_write('QUARANTINED',before='SELECTED')
        self.assertEqual(self.row('SELECT state FROM playerbot_autonomy_enrollment WHERE character_guid=2'),('PENDING',))
    def test_unmanaged_guild_and_audit_failure_cannot_commit_intent(self):
        with self.connection.cursor() as cur: cur.execute("INSERT INTO guild VALUES(7,1,'Bangerzoids')")
        self.connection.commit()
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('SELECTED')
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_recruitment'),(0,))
        with self.connection.cursor() as cur: cur.execute('DELETE FROM guild')
        self.connection.commit()
        self.managed()
        with self.assertRaises(self.driver.MySQLError): self.recruit_write('SELECTED',fail_audit=True)
        self.assertEqual(self.row('SELECT COUNT(*) FROM playerbot_autonomy_recruitment'),(0,))
