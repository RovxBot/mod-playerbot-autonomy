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

The playerbots v1 interface is now merged into **RovxBot/mod-playerbots
master** at `08b6bd127513d7273b1fcb765e537c5a3cd813ec`.
The maintenance changes were ported onto the original master branch, preserving
the newer profession and guild-bank deposit routines. Autonomy's standalone
provider tests are pinned to that **actual merged master** commit.
The target core used in homelab remains `test-staging`; source-level checks
do **not** replace a complete build/link using the production core/module pair.
Dungeon-clear master was inspected at `1a336ad58debdc2ffee598e78ce216ccff35153f`;
it is not changed or required by enrollment.

`RoutineMaintenancePolicy.h` v1 must be visible when compiling autonomy, with both
modules statically linked into the same worldserver. When the feature is enabled without that interface (or when registration
conflicts with another provider), the server intentionally **aborts** at
startup/configuration rather than risking reset of enrolled characters.
Provider ownership cannot overwrite another provider. The provider is
installed during script registration, before world
initialization/configured bot factories. Until the cache loads, unknown candidates
are vetoed. Verify this ordering in the integrated server before enabling.

`PlayerbotAutonomy.Enable = 0` stops activities and new transitions. **Protection
stays active**, on reload and on restart. This module now always requires the M1
schema and maintenance provider. Until the cache loads, unknown candidates are
blocked; schema/DB failures remain blocked. Known unowned filler uses legacy
maintenance and automatic guild placement while activities are off. Existing
Pending/Persistent characters stay protected even after guild loss, account
reassignment, or activity disable. A queued write may finish and its receipt and
cache are reconciled even after activities stop. No automatic retirement occurs.

The optional enrollment handoff requires playerbots master companion commit
`edbd9edbd49a387bdec6c71d97f15cce5b8df4e9` on baseline
`08b6bd127513d7273b1fcb765e537c5a3cd813ec`. It supplies cached explicit type-1
classification, a per-GUID login veto at ingress and asynchronous materialization,
and a pending-login observation. Missing handoff capability disables reservations;
missing maintenance capability aborts even with activities off. No full build or
deployment was run here. The user reports M1 deployed; that report is distinct
from this candidate's verification.

`RandomBotAccounts` is filled by factory character creation and is a broad pool;
it can include AddClass/unassigned accounts or omit existing dedicated accounts.
An active client account session excludes even an offline sibling character.
Autonomy uses `IsDedicatedRandomAccount()` from the manager's explicit type-1
assignments, which load independently of factory creation. It never authorizes an
account from a name prefix or calls the synchronous `IsAccountType()` on ticks.
Diagnosis reports both broad factory membership and dedicated eligibility. The
supplied Trela identity is covered with eligible/ineligible fixtures; live account
assignment, online state and historical rejection remain unverified.

A reservation holds one GUID for at most 120 seconds, vetoes competing bot logins
and maintenance, waits for in-flight login cleanup, and uses normal
`LogoutPlayerBot` save/logout before offline STAGE. No code writes
`characters.online`. Handoff to ordinary bot login is possible only after a
protected, durably loaded Pending record. Reservation expiry releases only
transient exclusion, never durable protection. On shutdown map workers stop before
the pool drains; provider lifetime continues until process exit.

Rollback: stop activities with Enable=0 and retain this protection revision and
all enrollment/audit/receipt tables. A code rollback to a revision that removes
protection is unsafe with Pending/Persistent rows: keep worldserver stopped until
compatible protection is restored. There is deliberately no configuration bypass
for existing durable protection. Initial DB open failure needs reload/restart;
established schema/page failures retry with backoff. Runtime disable cannot prove
that removing the module or downgrading an external binary is safe.

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

Only local console or administrator-authenticated RA/SOAP requests are accepted;
player/GM chat is rejected. On this core, CLI/RA/SOAP commands are all dispatched
through a sessionless CliHandler (normal in-game RBAC checks do not distinguish
them). The high-privilege command metadata is not a sufficient authorization
boundary on its own: **PlayerbotAutonomy.AllowEnrollmentCommands=1** must also
be set deliberately by an operator before lifecycle mutations are accepted.
This option defaults to 0, independently of the maintenance-protection switch.

This is an administrative M1 lifecycle, **not an autonomous guild director**.
There is no automatic adoption, recruitment scan, guild creation/join/leave,
reward generation, teleport or immediate reroll. Normal guild APIs/authorized
operator actions remain responsible for the separate guild step.

```
autonomy status
autonomy diagnose GUID
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

`autonomy diagnose GUID` takes one positive decimal character GUID and reads only
current world-thread facts. It works with enrollment mutations disabled and
reports enabled/readiness/provider status, whether the autonomy cache contains
the GUID, its IdentityValid/account/guild/state/revision, current
`IsInRandomAccountList` membership for the account used by validation,
`sCharacterCache` account/guild/group GUID (raw numeric value), connected Player
and WorldSession presence, and the first failing STAGE eligibility check.
An offline character reports WorldSession as `not_applicable`.
Cache presence includes invalidated entries; it does not imply valid identity.
Dedicated type-1 assignments are the runtime authority; broad factory membership
is reported separately and is not an enrollment eligibility proof.

The diagnosis and pre-commit validator share the same observations and decision
logic. Diagnosis uses the **current cached revision**, with no request UUID,
reason or target guild supplied. `stage_world_checks=PASS` means current
world-thread eligibility only: it does not check command mutation opt-in, queue
availability, a supplied revision/UUID/reason, replay, or SQL commit prerequisites
(including saved `characters.online`, deletion, guild/group rows and CAS).
It does not explain a historical rejection if the runtime facts have changed.
Diagnosis issues no SQL, queues/retries no STAGE, changes no cache entry or
counter, and writes no character, guild, group, enrollment, receipt or audit data.
Actual asynchronous pre-commit rejections now log the request UUID, GUID, event
and the same named rejection reason; post-commit missing receipts retain their
existing fail-closed recovery and are never retried as writes.

Diagnosis retains the console-only handler and administrator command metadata;
all in-game sessions, including GMs, are rejected. The pinned core checks SOAP
accounts against `SEC_ADMINISTRATOR` and authenticates RA according to
`Ra.MinLevel` (default 3), then discards caller identity before world-thread
dispatch. Therefore RA must admit only administrators, and diagnosis refuses
all sessionless origins while `Ra.MinLevel < 3`, including local console/SOAP:
the handler cannot distinguish these origins or recheck individual RA accounts.
Keep administrator-only RA admission for the lifetime of its sessions.
No core or dependency authorization checks are changed.

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
- Diagnosis regression tests exercise every runtime validation guard, policy
  rejections, named asynchronous reasons, and the production command handler's
  session/admin-admission and GUID parsing checks. They compare diagnosis output
  with actual pre-commit validation, including account 19 / guild 1 / GUID 187
  fixtures with both random-account membership outcomes. Repeated diagnosis
  leaves DB calls, commits, receipts, cache version/records and counters unchanged.
  These fixtures do not establish Trela's actual runtime rejection reason.
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
