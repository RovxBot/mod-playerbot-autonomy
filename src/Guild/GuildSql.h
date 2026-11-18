#ifndef PLAYERBOT_AUTONOMY_GUILD_SQL_H
#define PLAYERBOT_AUTONOMY_GUILD_SQL_H
namespace PlayerbotAutonomy::GuildSql
{
inline constexpr char Load[] = R"SQL(
SELECT m.*, EXISTS (
 SELECT 1 FROM playerbot_autonomy_enrollment_audit a
 JOIN playerbot_autonomy_transition_receipt r ON r.request_id=a.request_id
 WHERE a.request_id=m.stage_request AND a.character_guid=m.founder_guid AND a.account_id=m.account_id
 AND a.event='STAGE' AND a.state_after='PENDING' AND COALESCE(a.guild_id,0)=0
 AND a.actor_kind='RECRUITMENT' AND a.actor_guid=m.founder_guid
 AND a.reason=CONCAT('director founding slot ',m.slot)
 AND r.expected_revision=m.stage_revision AND r.resulting_revision=m.stage_revision+1
) stage_verified FROM (SELECT 1) s LEFT JOIN
 (SELECT slot, founder_guid, account_id, faction, guild_id, phase, name, reason, stage_revision, stage_request
  FROM playerbot_autonomy_guild ORDER BY slot LIMIT 64) m ON TRUE
WHERE EXISTS (SELECT 1 FROM information_schema.table_constraints
 WHERE table_schema=DATABASE() AND table_name='playerbot_autonomy_guild_audit'
 AND constraint_name='chk_autonomy_guild_mutation' AND enforced='YES')
)SQL";
inline constexpr char AuditPage[] = R"SQL(
SELECT g.guildid FROM (SELECT 1) s LEFT JOIN
 (SELECT guildid FROM guild WHERE guildid > ? ORDER BY guildid LIMIT 1) g ON TRUE
)SQL";
inline constexpr char Insert[] = R"SQL(
INSERT INTO playerbot_autonomy_guild (slot,founder_guid,account_id,faction,name,phase,reason,stage_revision,stage_request)
SELECT ?,c.guid,c.account,?,?,'PLANNED',?,?,? FROM characters c
WHERE c.guid=? AND c.account=? AND c.deleteDate IS NULL
 AND NOT EXISTS (SELECT 1 FROM guild_member WHERE guid=c.guid)
 AND NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid=c.guid)
)SQL";
inline constexpr char Change[] = R"SQL(
UPDATE playerbot_autonomy_guild m SET phase=?,guild_id=NULLIF(?,0),reason=?,changed_at=CURRENT_TIMESTAMP(6)
WHERE slot=? AND founder_guid=? AND account_id=? AND phase=?
 AND (? <> 'MANAGED' OR EXISTS (
  SELECT 1 FROM guild g JOIN guild_member gm ON gm.guildid=g.guildid AND gm.guid=m.founder_guid
  JOIN characters c ON c.guid=gm.guid AND c.account=m.account_id AND c.deleteDate IS NULL
  WHERE g.guildid=? AND g.leaderguid=m.founder_guid AND CONVERT(g.name USING utf8mb4) COLLATE utf8mb4_unicode_ci=m.name))
)SQL";
inline constexpr char Audit[] = R"SQL(
INSERT INTO playerbot_autonomy_guild_audit (slot,phase,guild_id,reason,mutation_applied)
VALUES (?,?,NULLIF(?,0),?,ROW_COUNT())
)SQL";
}  // namespace PlayerbotAutonomy::GuildSql
#endif
