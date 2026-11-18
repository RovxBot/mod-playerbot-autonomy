#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_SQL_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_SQL_H

// These exact prepared statements are also executed by the disposable MySQL tests.
namespace PlayerbotAutonomy::Sql
{
inline constexpr char Schema[] = R"SQL(
SELECT version FROM playerbot_autonomy_runtime_schema WHERE version = 1
 AND EXISTS (SELECT 1 FROM information_schema.table_constraints
     WHERE table_schema = DATABASE() AND table_name = 'playerbot_autonomy_transition_receipt'
       AND constraint_name = 'chk_autonomy_single_mutation' AND enforced = 'YES')
)SQL";

// LEFT JOIN to a sentinel distinguishes an empty healthy page from SQL failure.
// The bounded union retains deleted-character enrollment rows for reconciliation.
inline constexpr char Page[] = R"SQL(
SELECT p.* FROM (SELECT 1) sentinel LEFT JOIN (
 SELECT ids.guid, c.account, COALESCE(g.guildid, 0) guildid,
        e.account_id, COALESCE(e.state, 'ABSENT') state, COALESCE(e.revision, 0) revision,
        COALESCE(e.guild_id_at_enrollment, 0) enrolled_guild,
        c.deleteDate
 FROM (
  SELECT guid FROM (
   (SELECT guid FROM characters WHERE guid > ? ORDER BY guid LIMIT ?)
   UNION (SELECT character_guid AS guid FROM playerbot_autonomy_enrollment
          WHERE character_guid > ? ORDER BY character_guid LIMIT ?)
  ) candidates
  ORDER BY guid LIMIT ?
 ) ids
 LEFT JOIN characters c ON c.guid = ids.guid
 LEFT JOIN guild_member g ON g.guid = ids.guid
 LEFT JOIN playerbot_autonomy_enrollment e ON e.character_guid = ids.guid
) p ON TRUE ORDER BY p.guid
)SQL";

inline constexpr char Receipt[] = R"SQL(
SELECT a.character_guid, a.account_id, a.event, a.state_before, a.state_after,
       COALESCE(a.guild_id, 0), a.actor_kind, COALESCE(a.actor_guid, 0), a.reason,
       r.expected_revision, r.resulting_revision
FROM (SELECT 1) sentinel
LEFT JOIN playerbot_autonomy_enrollment_audit a ON a.request_id = ?
LEFT JOIN playerbot_autonomy_transition_receipt r ON r.request_id = a.request_id
)SQL";

inline constexpr char Stage[] = R"SQL(
INSERT INTO playerbot_autonomy_enrollment
 (character_guid, account_id, state, guild_id_at_enrollment, revision)
SELECT c.guid, c.account, 'PENDING', NULLIF(?, 0), 1
FROM characters c
WHERE c.guid = ? AND c.account = ? AND c.deleteDate IS NULL AND c.online = 0
 AND NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid = c.guid)
 AND NOT EXISTS (SELECT 1 FROM playerbot_autonomy_enrollment WHERE character_guid = c.guid)
)SQL";

// The world-thread authorization is rechecked against character/account, guild,
// group and revision facts in SQL. No core character data is ever written.
inline constexpr char Transition[] = R"SQL(
UPDATE playerbot_autonomy_enrollment e
JOIN characters c ON c.guid = e.character_guid AND c.account = e.account_id AND c.deleteDate IS NULL
LEFT JOIN guild_member g ON g.guid = c.guid
SET e.state = ?, e.revision = e.revision + 1,
    e.guild_id_at_enrollment = IF(? = 'STAGE', NULLIF(?, 0), e.guild_id_at_enrollment),
    e.stage_started_at = IF(? = 'STAGE', CURRENT_TIMESTAMP(6), e.stage_started_at),
    e.first_enrolled_at = IF(? = 'CONFIRM', COALESCE(e.first_enrolled_at, CURRENT_TIMESTAMP(6)), e.first_enrolled_at),
    e.state_changed_at = CURRENT_TIMESTAMP(6)
WHERE e.character_guid = ? AND e.account_id = ? AND e.revision = ? AND e.state = ?
 AND ((? = 'CONFIRM' AND g.guildid = ? AND g.guildid > 0)
   OR (? IN ('CANCEL', 'RETIRE') AND g.guid IS NULL
       AND NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid = c.guid))
   OR (? = 'STAGE' AND c.online = 0 AND NOT EXISTS (SELECT 1 FROM group_member WHERE memberGuid = c.guid)))
)SQL";

// Must immediately follow the mutation on the SAME transaction connection.
// A CAS miss causes a CHECK failure, rolling back the entire transaction. A
// duplicate request also rolls it back; the caller then verifies the receipt.
inline constexpr char InsertReceipt[] = R"SQL(
INSERT INTO playerbot_autonomy_transition_receipt
 (request_id, expected_revision, resulting_revision, mutation_applied)
VALUES (?, ?, ?, ROW_COUNT())
)SQL";

inline constexpr char Audit[] = R"SQL(
INSERT INTO playerbot_autonomy_enrollment_audit
 (request_id, character_guid, account_id, event, state_before, state_after,
  guild_id, actor_kind, actor_guid, reason)
VALUES (?, ?, ?, ?, ?, ?, NULLIF(?, 0), ?, NULLIF(?, 0), ?)
)SQL";

inline constexpr char Orphan[] = R"SQL(
INSERT INTO playerbot_autonomy_enrollment_audit
 (request_id, character_guid, account_id, event, state_before, state_after,
  guild_id, actor_kind, actor_guid, reason)
SELECT ?, e.character_guid, e.account_id, 'ORPHAN_RECONCILE', e.state, e.state,
 e.guild_id_at_enrollment, 'SYSTEM_RECONCILER', NULL, 'Missing character or changed account; history retained'
FROM playerbot_autonomy_enrollment e LEFT JOIN characters c ON c.guid = e.character_guid
WHERE e.character_guid = ? AND (c.guid IS NULL OR c.deleteDate IS NOT NULL OR c.account <> e.account_id)
 AND NOT EXISTS (SELECT 1 FROM playerbot_autonomy_enrollment_audit WHERE request_id = ?)
)SQL";
}  // namespace PlayerbotAutonomy::Sql
#endif
