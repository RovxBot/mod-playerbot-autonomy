#ifndef PLAYERBOT_AUTONOMY_OPERATION_SQL_H
#define PLAYERBOT_AUTONOMY_OPERATION_SQL_H
namespace PlayerbotAutonomy::OperationSql
{
inline constexpr char Insert[] = R"SQL(
INSERT INTO playerbot_autonomy_operation
 (operation_id,revision,guild_id,leader_guid,faction,operation_type,state,created_at,changed_at,deadline,lease_until,attempts,reason)
VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)
)SQL";
inline constexpr char Change[] = R"SQL(
UPDATE playerbot_autonomy_operation SET revision=?,state=?,changed_at=?,attempts=?,reason=?
WHERE operation_id=? AND revision=? AND state=?
)SQL";
inline constexpr char Journal[] = R"SQL(
INSERT INTO playerbot_autonomy_operation_journal (operation_id,revision,state,reason,mutation_applied)
VALUES (?,?,?,?,ROW_COUNT())
)SQL";
inline constexpr char Member[] = R"SQL(
INSERT INTO playerbot_autonomy_operation_member
 (operation_id,position,character_guid,account_id,guild_id,combat_role) VALUES (?,?,?,?,?,?)
)SQL";
// A missing identity/membership/protection proof produces NULL in a NOT NULL
// column and aborts the entire transaction, including every preceding lease.
inline constexpr char Lease[] = R"SQL(
INSERT INTO playerbot_autonomy_operation_lease (character_guid,operation_id,expires_at,account_id)
VALUES (?,?,?,(SELECT c.account FROM characters c
 JOIN playerbot_autonomy_enrollment e ON e.character_guid=c.guid AND e.account_id=c.account AND e.state='PERSISTENT'
 JOIN guild_member gm ON gm.guid=c.guid
 JOIN playerbot_autonomy_guild m ON m.guild_id=gm.guildid AND m.phase='MANAGED'
 JOIN guild g ON g.guildid=m.guild_id AND g.leaderguid=m.founder_guid
 WHERE c.guid=? AND c.account=? AND gm.guildid=? AND c.deleteDate IS NULL
 AND BINARY CONVERT(g.name USING utf8mb4)=BINARY m.name
 AND NOT EXISTS (SELECT 1 FROM group_member grouped WHERE grouped.memberGuid=c.guid)))
)SQL";
inline constexpr char Release[] = R"SQL(
DELETE FROM playerbot_autonomy_operation_lease WHERE operation_id=?
)SQL";
inline constexpr char Execution[] = R"SQL(
INSERT INTO playerbot_autonomy_execution
 (operation_id,revision,execution_mode,group_id,creation_fenced,succeeded,group_confirmed) VALUES (?,?,?,?,?,?,?)
ON DUPLICATE KEY UPDATE revision=VALUES(revision),execution_mode=VALUES(execution_mode),
 group_id=VALUES(group_id),creation_fenced=VALUES(creation_fenced),succeeded=VALUES(succeeded),
 group_confirmed=VALUES(group_confirmed)
)SQL";
// Read-only core evidence. The module never creates/deletes core group SQL.
inline constexpr char GroupSnapshot[] = R"SQL(
SELECT g.leaderGuid,m.memberGuid,m.guid FROM (SELECT 1) s LEFT JOIN `groups` g ON g.guid=?
LEFT JOIN group_member m ON m.memberGuid IN (?,?,?,?,?) ORDER BY m.memberGuid LIMIT 6
)SQL";
inline constexpr char GroupAbsent[] = R"SQL(
SELECT CAST(NOT EXISTS (SELECT 1 FROM `groups` WHERE guid=? OR leaderGuid IN (?,?,?,?,?)) AND
 NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid IN (?,?,?,?,?)) AS UNSIGNED)
)SQL";
inline constexpr char Load[] = R"SQL(
SELECT o.*,m.position,m.character_guid,m.account_id,m.guild_id,m.combat_role,
 (SELECT COUNT(*) FROM playerbot_autonomy_operation_lease l WHERE l.operation_id=o.operation_id) lease_count,
 x.schema_version,x.revision,x.execution_mode,x.group_id,x.creation_fenced,x.succeeded,x.group_confirmed
FROM (SELECT 1) s LEFT JOIN playerbot_autonomy_operation o ON o.operation_id=?
LEFT JOIN playerbot_autonomy_operation_member m ON m.operation_id=o.operation_id
LEFT JOIN playerbot_autonomy_execution x ON x.operation_id=o.operation_id
ORDER BY m.position LIMIT 26
)SQL";
inline constexpr char RecoverPage[] = R"SQL(
SELECT o.operation_id FROM (SELECT 1) s LEFT JOIN
 (SELECT operation_id FROM playerbot_autonomy_operation
 WHERE operation_id>? AND state NOT IN ('COMPLETED','FAILED','CANCELLED') ORDER BY operation_id LIMIT 1) o ON TRUE
WHERE EXISTS (SELECT 1 FROM information_schema.table_constraints
 WHERE table_schema=DATABASE() AND table_name='playerbot_autonomy_operation_journal'
 AND constraint_name='chk_autonomy_operation_mutation' AND enforced='YES')
AND EXISTS (SELECT 1 FROM information_schema.table_constraints
 WHERE table_schema=DATABASE() AND table_name='playerbot_autonomy_execution'
 AND constraint_name='chk_autonomy_execution_version' AND enforced='YES')
AND EXISTS (SELECT 1 FROM playerbot_autonomy_execution
 WHERE operation_id=0 AND schema_version=1 AND execution_mode=0 AND revision=0)
)SQL";
}  // namespace PlayerbotAutonomy::OperationSql
#endif
