# M1 persistent enrollment contract (issue #5)

**Status:** schema migration, pure C++17 lifecycle decisions and static/standalone
tests only. No SQL queries, cache, recruitment, retirement command, playerbots
hooks, migrations applied to a real DB, or live persistence protection have
been implemented. Shipping this code alone does **not** make bots persistent.

This contract is based on:
- playerbots AzerothCore core **test-staging** source; specifically
  `UpdateFetcher::ReceiveIncludedDirectories`, which scans enabled modules'
  `data/sql/db-characters/` for the **characters** database;
- `RovxBot/mod-playerbots` **master** at
  `59e5f26d417fca173d969cd6a1064ec9638b1afa`;
- the maintenance audit in [MAINTENANCE_AUDIT.md](MAINTENANCE_AUDIT.md).

## Goals and identity

A guild-managed bot must keep the actual character record: level, earned XP,
equipment, durability, gold, talents, professions, learned spells, inventory,
quest flags, achievements and location. Enrollment never creates rewards or
resets a character.

**Stable identity is the character GUID plus a validated bot account.**
The account is verified against authoritative character/account records and
the configured random-bot account list; it is **not** derived solely from
`RandomPlayerbotMgr::IsRandomBot` because that API depends on transient
`currentBots` membership. At commit time, recheck the current account
mapping; a GUID transferred to a non-random account must not be treated as
an authorized bot.

Store the character GUID and account ID as unsigned 32-bit IDs matching this
WotLK fork's character DB. The account ID is a **snapshot to verify**, not an
authorization proof by itself. Do not persist player or guild pointers.

## State machine

| State | Stored DB row? | Destructive maintenance (when protection is enabled and bot account verified) | Meaning |
| --- | --- | --- | --- |
| `Unknown` | unavailable/not loaded | **Block (fail closed)** | No trustworthy cache/DB answer; never assume absent |
| `Absent` | no (confirmed query) | Legacy policy, except if currently guilded/leased | Never durably enrolled |
| `Pending` | yes | **Block** | Durable reservation created **before** guild change |
| `Persistent` | yes | **Block** | Guild join confirmed, or explicitly authorized adoption completed |
| `Retired` | yes (tombstone) | Legacy policy if not currently guilded/leased | Explicit retirement or safe cancellation of failed enrollment |

Allowed transitions:

~~~text
    confirmed filler or retired (no active group/job/lease)
                 |
      authorized STAGE + durable DB commit
                 v
              PENDING  -- guild AddMember / existing guild adoption -->
                 |                             |
       failed / no guild                    confirmed guild
        and no lease                           |
                 |                         CONFIRM commit
          authorized CANCEL                     |
                 v                              v
              RETIRED                       PERSISTENT
                 ^                              |
                 |                   accidental guild loss, transfer,
                 |                     disband, logout, restart:
                 |                           STILL PERSISTENT
                 |                              |
                 +-------- explicit RETIRE -----+
                         only after verified
                         guild leave, no group/job/lease
~~~

An idempotent `STAGE` of a Pending record or `CONFIRM` of a Persistent
record does not create another transition. **Idempotency requires more than
checking state:** a reused request ID for a different character/event must
be rejected by the database adapter.

**Critical crash window:** persist Pending and publish cache protection
**before** invoking the guild change. On restart, a Pending row protects the
character even if AddMember partly completed. A reconciler can confirm only
when the authoritative guild membership is present; otherwise retain Pending
until an authorized safe cancellation proves no guild/lease/job/group exists.
Do **not** auto-cancel merely because a timeout passed.

- Guild transfer, missing guild row or accidental disband never calls
  retirement. Persisted membership protection survives them all.
- A guilded bot in a human-managed guild can be progression-protected, but
  the guild **cannot be autonomously directed**; revalidate ownership and
  *all* human membership at the actual authorization point.
- Current automatic random-bot `InitGuild()` on login must be gated and
  replaced by explicit recruitment before enabled mode. No blanket migration
  of every existing bot to Persistent.
- Directing a human player, active selfbot, or bot with human master is
  forbidden. Disabled mode always defers to original playerbots behaviour.

## Schema migration (not yet executed)

`data/sql/db-characters/2026_11_15_00_playerbot_autonomy_enrollment.sql`
creates exactly two module-owned InnoDB tables in the **characters** DB:

- `playerbot_autonomy_enrollment`: one row per character GUID; account ID;
  Pending/Persistent/Retired; guild ID at enrollment (an audit snapshot, not a
  currently valid guild); revision; reservation/enrollment/change times.
- `playerbot_autonomy_enrollment_audit`: append-only transitions keyed by a
  unique 36-character **request_id**. Includes character, account, before/after
  state, event, actor kind/ID, reason and timestamp. Reconciliation findings
  also belong here.

The migration only runs `CREATE TABLE IF NOT EXISTS`. It never seeds
existing bot characters, touches the core `characters` or `guild` tables,
adds FKs/cascades, modifies playerbots' database, or enables the feature.
A standalone test checks the structure and SQL file location. **That is NOT a
live MySQL execution or a guarantee of schema compatibility with an existing
table.** The core's updater normally tracks applied module update filenames.
Before deployment, verify the updater sees this file and executes it in a
throwaway MySQL 8.x database. Avoid applying an untested migration directly
to the live realm. Ensure the image/import workflow ships module SQL and
updates are enabled at the intended import stage.

No ON DELETE CASCADE: character deletion must not erase audit history. A
bounded reconciler must detect orphan GUIDs without repeatedly logging or
running joins on bot ticks. Mark orphan discoveries once with a stable
request ID; quarantine/delete records only through a separately documented
audited cleanup, never automatically treat an unknown account as filler.

## Adapter/transaction recipe for the next implementation

The following is a **requirement**, not implemented code:

1. Initialize the enrollment cache and loaded/unknown state on the world
   thread before any persistent reset checks are enabled. Load all rows in
   bounded pages off map threads; reject missing/corrupt tables, DB outages
   and stale snapshots. Validate current character-account identity.
2. Accept commands only through an independently authorized, non-human
   director/admin path (not spoofed GM/chat). Verify random-bot account
   membership, non-selfbot, no human controller, guild ownership and lease/
   group status using current world-thread state.
3. Generate a unique idempotency key for the *specific* transition. In a DB
   transaction, check audit replay by request_id and row revision (lock row
   or optimistic CAS). Reject GUID/account mismatch and conflicting replay.
4. STAGE: insert/update Pending row and append audit, commit, then publish
   Pending to the in-memory cache **before attempting** the guild change.
   If stage cannot be confirmed, do NOT proceed with guild mutation.
5. CONFIRM: after the normal guild API has completed, verify authoritative
   membership and the Pending row, commit Persistent + audit atomically,
   then publish to the cache. A crash between join and confirm leaves
   Pending; reconciliation is safe.
6. CANCEL: only when no guild membership, group, operation or lease remains,
   write a Retired tombstone and audit with a **new request ID**. This
   deactivates a failed reservation; it must not wipe any character data.
7. RETIRE: only with a separate explicit authorized request and after a
   completed guild exit and no group/lease/operation; commit Retired + audit.
   Do not call a randomizer or reset as part of retiring. Returning a bot
   to normal filler merely makes future legacy maintenance *eligible*.
8. Update cache only after commit. Failed/unknown writes remain protected
   Pending/Unknown; retry safely, with bounded queue and counters. Avoid
   synchronous database calls in `PlayerbotAI`/map-tick paths.
9. Reconcile on restart (and bounded periodic sweeps): Pending with confirmed
   guild may finalize; Persistent with lost guild stays Persistent; Retired
   with unexpected guild must not be destructively refreshed; orphan or
   account mismatch must quarantine and log rather than auto-retire.

**Atomicity caution:** the core's guild add/remove API may commit in its own
transaction, so you cannot assume the guild change and enrollment write share
one transaction. The durable Pending stage is the explicit crash barrier.
Cache invalidation and world-thread ownership must be verified in the actual
core; no world object reads should be made from an arbitrary DB callback.

## Verification matrix

- **Pure tests (now present):** stage/confirm/cancel/retire, idempotent
  state response, guild-loss retention, invalid account, selfbot/human,
  actor authorization, missing request ID, busy operation/lease/group,
  unknown fail-closed, disabled/ordinary-filler no autonomy veto.
- **Static SQL checks (now present):** filename and discoverable path;
  exactly two `CREATE TABLE IF NOT EXISTS` InnoDB statements; primary key,
  unique request key, state/event names and no data-seeding statements.
- **Still required with MySQL + runtime adapter:** real migration up/down
  compatibility, transaction rollback/failure, unique request replay, race
  handling, stale revision rejection, restart and pending recovery, orphan
  detection, account reassignment, live guild transfer/disband, manual
  retirement audit, no item/XP/gold changes and benchmarked hot lookup.
- **Still required in playerbots:** optional default-off veto at all
  destructive entry points in the maintenance audit, suppression of legacy
  auto-guild placement only after safe recruitment integration, and no
  changed behaviour when autonomy is disabled/missing.

## Deployment and rollback

Do **not** enable new runtime functionality by merging this PR. Its C++ code
is header-only and never called from `AutonomyModule.cpp`; no new toggles
are introduced.

Roll back application code by disabling `PlayerbotAutonomy.Enable` and
reverting the code/PR. **Do not drop the two tables as a routine rollback:**
that destroys the only evidence of durable enrollment and retirement.
Keep backups and migration history. If a future migration must be reversed,
perform a separate reviewed procedure preserving character state and audit.
