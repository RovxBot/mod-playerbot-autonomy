# M1 runtime candidate: enrollment and maintenance protection

The adapter, reconciliation/cache and explicit console transitions are implemented
for review. SQL has been exercised against a disposable MySQL **8.4.10** instance.
Deterministic controller tests use engine/DB boundary fakes. Neither those tests
nor source syntax checks establish game-server persistence. **No full linking,
startup, controlled bot pilot, deployment or live database changes were performed.**
Issues #5 and #17 remain open for those acceptance gates.

## Compatibility and activation

Foundation: `a9703971066e1ab067eb5e90d9d5517eb88865ae` (unmerged foundation PR #16).
This runtime change is stacked on that branch; retarget after its merge.

Inspected core: `mod-playerbots/azerothcore-wotlk` **test-staging**
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`. It provides `ModuleDatabasePool`,
prepared statements, async commit results, WorldScript and GUID-aware guild hooks.
The new module source uses C++17 constructs; that core's headers require GNU
C++20. C++17/C++20 policy/controller tests do not establish compatibility with an
older deployed core that lacks `ModuleDatabasePool`.

The playerbots v1 implementation is pinned at
`3beeabf9735766edfa24abf9032b20e8c2bb25e2` (source branch
`feature/optional-maintenance-policy` in `RovxBot/mod-playerbots`). It targets the
required **test-staging** branch at
`889e9dd6e354d87d11933a093c709016b110e179`. Production module **master** at
`59e5f26d417fca173d969cd6a1064ec9638b1afa` is **184 commits ahead** of that base.
The production core branch reported by the owner is `test-staging`, although
playerbots' documented pairing says module master/core Playerbot. This existing
combination is not assumed compatible. A master port artifact, syntax checks and
boundary tests are preparation for maintainer promotion, not a deployment signoff.
Dungeon-clear master was inspected at `1a336ad58debdc2ffee598e78ce216ccff35153f`;
it is not changed or required by enrollment.

`RoutineMaintenancePolicy.h` v1 must be visible when compiling autonomy, with both
modules statically linked into the same worldserver. Without it, the adapter never
opens its pool or accepts enrollment. Provider ownership cannot overwrite another
provider. The provider is installed during script registration, before world
initialization/configured bot factories. Until the cache loads, unknown candidates
are vetoed. Verify this ordering in the integrated server before enabling.

`PlayerbotAutonomy.Enable = 0` remains the default: disabled startup opens no
enrollment DB connection and accepts no transitions, guild hooks with effects,
or maintenance provider. A pool opened during an enabled session is retained
until shutdown, while disabled mode schedules no new SQL. A reload to 0
removes the provider immediately and prevents new work. An already submitted SQL
transaction may finish; its history is retained. Re-enable rebuilds readiness.
An initial pool-open/prepare failure requires a config reload or restart to retry; failures
on established connections/schema/page queries retry with a five-second backoff.

Disabling intentionally restores legacy maintenance, including for enrolled bots.
For rollback of an enabled pilot, first pause destructive random maintenance or
stop the realm, preserve all enrollment/audit/receipt tables, then revert code.
Dropping tables, deleting history or retiring every bot is not a rollback.

## SQL and transactional boundary

The core's inspected `UpdateFetcher::ReceiveIncludedDirectories` enumerates enabled
module `data/sql/` directories matching `db-characters`. Both module migrations
are in that discoverable directory. Discovery was verified from the pinned source;
the complete core updater executable has not been run.

Apply both migrations in order to a disposable MySQL 8 database before rollout.
The second migration fixes the foundation audit's missing `PENDING` outcome. It
also adds a module-owned receipt table and schema marker; it does not rewrite
existing audit history or invent revisions for old receipts. Both migrations were
executed twice successfully. Runtime startup requires an **enforced** receipt
CHECK constraint (MySQL 8.0.16+), not merely a table with the right name.

`EnrollmentStore` owns one async worker plus a connection used for open/prepare,
using the existing `CharacterDatabaseInfo` credentials. Its prepared statements
and transactions use core `ModuleDatabasePool`; it does not extend core enums,
patch base schemas, or write character progression/guild/group tables.

A transition transaction executes:

1. Insert Pending for confirmed Absent, or update with GUID/account/state/revision
   CAS. Recheck the character account/deletion, guild and group rows in SQL.
2. Immediately insert a receipt with `ROW_COUNT()`. An enforced CHECK requires
   exactly one mutation. A stale revision or failed prerequisite rolls back.
3. Insert the append-only audit. A duplicate request ID or failed audit also rolls
   everything back. Read the durable receipt after completion, including failure
   or an uncertain COMMIT acknowledgement; do not blindly retry writes.

Replay verifies character, account, action, resulting state, expected/resulting
revision, guild, actor and reason. An old STAGE replay after CONFIRM never replaces
the newer cache. Historical foundation audit entries without runtime receipts are
not accepted as successful runtime replays. Failed/rejected requests with no
committed receipt may be retried; only committed transitions reserve their ID.

## Explicit console lifecycle

Only console/authenticated RA requests are accepted; player/GM chat is rejected.
This is an administrative M1 lifecycle, **not an autonomous guild director**.
There is no automatic adoption, recruitment scan, guild creation/join/leave,
reward generation, teleport or immediate reroll. Normal guild APIs/authorized
operator actions remain responsible for the separate guild step.

```
autonomy status
autonomy stage   GUID EXPECTED_REVISION GUILD_ID REQUEST_UUID reason
autonomy confirm GUID EXPECTED_REVISION GUILD_ID REQUEST_UUID reason
autonomy cancel  GUID EXPECTED_REVISION 0        REQUEST_UUID reason
autonomy retire  GUID EXPECTED_REVISION 0        REQUEST_UUID reason
```

GUIDs are low character IDs. New enrollment uses revision 0; every committed
transition increments it. Use a new lowercase UUID for each transition. Queue
acknowledgement is not success: verify the request's **durable receipt** log and
wait for `autonomy status` readiness before changing guild membership. Keep the
same UUID and identical arguments when retrying an uncertain request.

- STAGE requires the character to be **offline**, a verified dedicated random-bot
  account and no group. SQL rechecks `characters.online = 0`. An already running
  factory cannot be interrupted by a callback, so online initial staging/re-staging
  is deliberately unavailable. Log out/save the bot through the normal engine
  path before staging; this is not a bot-login or guild-recruitment controller.
  No progression or guild data changes. Existing guild membership may be
  explicitly adopted for protection; this grants no guild governance authority.
- Only after the stage receipt and protected cache are ready may the independently
  authorized guild change run. CONFIRM rechecks online AI/human-control boundaries when connected, cached
  offline identity otherwise, and the committed `guild_member` row. If that row is not durable yet, confirmation
  fails safely; Pending continues protecting the character.
- Failed recruitment stays Pending through restart. CANCEL requires verified no
  guild, normal/original group, lease or operation. It creates a Retired tombstone.
- Guild transfer, missing guild, disband and logout never retire Persistent.
  RETIRE is a separate explicit authorized transition after completed guild exit
  and no group/activity. It writes only enrollment/audit/receipt state.

M1 has no activity/lease producer. Future operation support must extend the same
validation and provider before exposing leases or recruitment from operations.
The runtime does not infer autonomous guild ownership from a leader account.
Human-managed guilds are never commanded or changed by these operations.

## Cache and reconciliation

The world thread owns scheduling and writes. Map-thread queries take a short
shared lock and copy one GUID entry; they never call SQL, read engine pointers,
load config, enumerate guilds or scan the realm. Missing/invalid identity is
Unknown, never confirmed Absent. Pending/Persistent, current guild membership and
unavailable snapshots veto destructive maintenance while enabled.

Bootstrap and periodic reconciliation read indexed keyset pages of 128 GUIDs,
including orphan enrollment rows. Each union input is bounded before deduplication.
At most one query/transition and one bounded page are processed in a world update;
orphan findings are serialized one at a time. A sweep restarts after 60 seconds.
Core cached account/guild facts are compared on the world thread because guild
SQL writes use another async pool. Event invalidations also beat in-flight old
pages. No cache state is published from a DB worker thread.

Pending is retained, not automatically cancelled or silently confirmed. A deleted
character/account mismatch remains quarantined; a stable `orphan-v1-GUID` audit
key records the finding once and keeps all enrollment history. No orphan cleanup
returns a GUID to the reset pool. New/missing GUIDs remain Unknown until observed.
Schema/page failures make the whole snapshot unavailable. A 30-second async timeout
blocks new work and remains fail-closed until the existing operation resolves;
one outstanding operation bounds queue growth. Shutdown drains the pool after
maps unload; it never stores a Player/Group pointer for recovery.

`autonomy status` exposes aggregate pages/rows, Pending/orphan observations,
commits, replays, rejections and failures. Observations are cumulative, not current
population counts. Maintenance veto counters are provided by the playerbots v1
interface. There is no per-AI-tick logging.

## Verification and remaining gates

Local evidence for this candidate:

- C++17/C++20 population/lifecycle, cache and receipt-identity tests.
- Actual controller compiled/executed with deterministic engine/DB boundary fakes,
  with the real optional interface and also with no interface. Covers disabled
  mode, humans/selfbots/control, Pending restart, replay/conflict, guild loss,
  retirement/group refusal, failed writes, schema/cache failure and timeout/reload.
- Disposable MySQL 8.4.10 (nine tests): migration replay, exact SQL receipts, offline-STAGE enforcement, Pending reload,
  guild transfer/disband, no level/money changes, stale revisions/concurrency,
  audit failure/rollback, safe retirement and idempotent orphan history.
- Source-extracted playerbots entry guards exercised under C++17/C++20; deferred
  queue/reclaim/group/bulk-delete checks and policy-registration tests. These use
  fake Player/Group boundaries and do not execute full server methods.
- Real-header syntax checks using `tools/check_runtime_syntax.py` against pinned
  test-staging and the isolated production-master port; no objects or linking.
- Local 10,000-entry cache lookup and optional callback microbenchmarks. These
  measure lookup overhead only, not realm pmon, map tick time or DB load.

Reproduce SQL tests with PyMySQL installed in a temporary environment:

```sh
AUTONOMY_MYSQL_SOCKET=/path/to/disposable/mysql.sock \
AUTONOMY_MYSQL_DATABASE=autonomy_test \
python3 -m unittest discover -s tests -p test_enrollment_mysql.py -v
```

Only a database named `autonomy_test` or `autonomy_test_suffix` is accepted. The
tests clear their disposable fixtures. Normal Python discovery skips these tests
unless explicitly configured. CI uses a disposable MySQL 8.4 service.

Before claiming acceptance: link the exact chosen source combination, verify
startup/reload/provider ordering, then run a controlled filler + Persistent +
interrupted-Pending pilot over repeated maintenance cycles and a realm restart.
Record XP, equipment, money, talents, skills, professions, quests and achievements;
exercise every console/factory/reset/teleport/trim bypass and normal XP/trainer,
loot, crafting, auction, guild-bank, RDF and dungeon-clear behaviour. Verify actual
DB-outage/shutdown recovery and measure pmon/CPU/map ticks/memory/DB load at scale.
The owner will coordinate that build and pilot separately. Do not close #5/#17
or enable production on the strength of standalone/source tests alone.
