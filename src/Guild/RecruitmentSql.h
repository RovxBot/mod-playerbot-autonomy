#ifndef PLAYERBOT_AUTONOMY_RECRUITMENT_SQL_H
#define PLAYERBOT_AUTONOMY_RECRUITMENT_SQL_H
namespace PlayerbotAutonomy::RecruitmentSql
{
inline constexpr char Load[] = R"SQL(
SELECT m.*, EXISTS (
 SELECT 1 FROM playerbot_autonomy_enrollment_audit a
 JOIN playerbot_autonomy_transition_receipt r ON r.request_id=a.request_id
 WHERE a.request_id=m.stage_request AND a.character_guid=m.character_guid AND a.account_id=m.account_id
 AND a.event='STAGE' AND a.state_after='PENDING' AND a.guild_id=m.guild_id
 AND a.actor_kind='RECRUITMENT' AND a.actor_guid=m.actor_guid
 AND a.reason=CONCAT('director recruitment guild ',m.guild_id)
 AND r.expected_revision=m.stage_revision AND r.resulting_revision=m.stage_revision+1
) stage_verified FROM (SELECT 1) s LEFT JOIN (
 SELECT character_guid,account_id,guild_id,slot,actor_guid,stage_revision,stage_request,phase,reason
 FROM playerbot_autonomy_recruitment WHERE character_guid>? ORDER BY character_guid LIMIT 64
) m ON TRUE
WHERE EXISTS (SELECT 1 FROM information_schema.table_constraints
 WHERE table_schema=DATABASE() AND table_name='playerbot_autonomy_recruitment_audit'
 AND constraint_name='chk_autonomy_recruit_mutation' AND enforced='YES')
)SQL";
inline constexpr char Insert[] = R"SQL(
INSERT INTO playerbot_autonomy_recruitment
 (character_guid,account_id,guild_id,slot,actor_guid,stage_revision,stage_request,phase,reason)
SELECT c.guid,c.account,?,?,?, ?,?,'SELECTED',? FROM characters c
JOIN playerbot_autonomy_guild m ON m.slot=? AND m.guild_id=? AND m.phase='MANAGED'
WHERE c.guid=? AND c.account=? AND c.deleteDate IS NULL
 AND NOT EXISTS (SELECT 1 FROM guild_member WHERE guid=c.guid)
 AND NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid=c.guid)
)SQL";
inline constexpr char Change[] = R"SQL(
UPDATE playerbot_autonomy_recruitment m SET phase=?,reason=?,changed_at=CURRENT_TIMESTAMP(6)
WHERE character_guid=? AND account_id=? AND guild_id=? AND actor_guid=? AND phase=?
 AND (?<>'COMPLETE' OR EXISTS (
   SELECT 1 FROM playerbot_autonomy_enrollment e JOIN guild_member gm ON gm.guid=e.character_guid
   WHERE e.character_guid=m.character_guid AND e.account_id=m.account_id AND e.state='PERSISTENT'
   AND gm.guildid=m.guild_id))
)SQL";
inline constexpr char Audit[] = R"SQL(
INSERT INTO playerbot_autonomy_recruitment_audit
 (character_guid,phase,guild_id,actor_guid,reason,mutation_applied)
VALUES (?,?,?,?,?,ROW_COUNT())
)SQL";
}  // namespace PlayerbotAutonomy::RecruitmentSql
#endif
