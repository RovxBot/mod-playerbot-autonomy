# M2 roster planning and simulated operations

The operator reports M1, four managed guilds (23–26), 20-member caps,
founding/recruitment, interrupted-founding recovery and restart persistence
accepted on the realm. This change builds on that baseline; it requires no
repeat M1/founding pilot and changes no existing guild identity or deployment
configuration. New M2 behaviour has **no combined worldserver build or live
acceptance** yet.

Sources: core test-staging `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`,
playerbots master `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`.
No dungeon-clear calls or companion changes are required for this slice.
The current dungeon-clear contract pin remains
`b1be76d93da4b409daa7a2c9a8f27301e540af4a`.

## What can run

`Operations.Enable=0` is the default. With the master activity switch and
`Operations.Enable=1`, default `Operations.DryRun=1` refreshes capability
snapshots and computes read-only party templates. Setting `DryRun=0` permits
**simulated** operations with real durable intents and exclusive leases.
`Operations.AssembleGroups=1` fails closed (`group_executor_unavailable`):
there is no game-group executor in this release. No group creation, movement,
teleport, dungeon entry, dungeon-clear launch, GM harness or rewards are called.
Future activity types are represented by the common model; only the five-person
simulation is accepted by this persistence adapter.

The planner requires one tank, one healer and three DPS, with matching faction,
compatible levels and real equipped average item level meeting the configured
floor. It evaluates every bounded level window and prefers same-guild members.
Cross-guild mixing defaults off. A party template does **not** establish access,
lockouts or readiness for a particular dungeon. Target-specific checks remain an
M3 gate; the minimum item-level setting is explicitly an operator threshold,
not an invented dungeon difficulty score.

Only explicit MANAGED guild intents, verified complete ownership/control checks,
dedicated type-1 accounts and M1 PERSISTENT members are eligible. Human guilds,
selfbots, human masters/sessions and world filler cannot be reserved. Fresh
world-thread observations revalidate selected members before intent creation
and every lifecycle advance. Logout, queue/group changes, guild/control changes,
combat, travel, death, instances and stale/unknown evidence fail closed.

`AiFactory::GetPlayerSpecTabs/GetPlayerSpecTab` and `PlayerbotAI::IsTank/IsHeal/
IsDps` supply role evidence. Untalented/tied talent allocations, multiple roles,
or disagreement between AI strategy and spec stay unknown. Offline roles and
gear stay unknown. `GetEquipGearScore` supplies its actual equipped **average
item level**; broken items or a missing main-hand weapon reject equipment
evidence. The 11 primary profession skill values come from the core.

## Thread and load ownership

One world scheduler maintains value-only snapshots. The pinned core calls
`OnWorldUpdate` after `MapMgr::Update`, whose map updater waits for workers.
No engine pointers are retained across updates or in SQL. Refreshing selects
one explicit guild (at most the existing 200-member hard cap), uses its existing
ownership audit, then samples at most `RefreshPerSecond` members per second.
Membership changes prune old snapshots; initial partial guild snapshots cannot
produce a plan. A hard `MaxCandidates<=512` bounds cached members. Old snapshots
expire; oversized configurations report capacity limits rather than scanning
the realm. Large realms must choose refresh budgets/age bounds that cover their
managed guilds; stale evidence is never made ready just to form a party.

The planner does at most 80 * 512 comparisons per planning pass. One coordinator
SQL action is scheduled per tick, one operation decision per interval, at most
four concurrent operations, and one async DB job outstanding. Capability
sampling has no SQL. Query failures back off five seconds; an outstanding
callback is retained after 30 seconds to resolve its real outcome. Diagnostics
count queries/writes/failures, conflicts, recoveries and last/max scheduler CPU
microseconds. These are source/harness measurements, not map latency or a realm
soak result. No per-AI-tick scanning or logging is added.

## Journal, leases and recovery

Apply `2026_11_19_01_playerbot_autonomy_operations.sql` through the ordinary
module character updater before using this revision. It creates four module
InnoDB tables; it does not seed, rewrite or rename guilds/characters. Retain the
schema on rollback. Startup opens the async store and recovers unfinished
operations **even with M2/master disabled**, so disabling across a restart does
not strand previous intents. With no interrupted work this is one sentinel
read and no operation writes. Missing schema/store disables M2; M1 continues.

Operations persist a random nonzero 63-bit identity, immutable ordered
participants/accounts/guilds/roles, type, faction, leader, timestamps, deadline,
fixed lease expiry, state, revision, attempt count and reason. A collision
cannot overwrite an existing identity: the primary key rejects the insert and
readback compares the complete immutable payload before accepting it.

Every state mutation uses revision/state compare-and-swap plus a journal insert
whose CHECK requires exactly one mutation. RESERVED inserts **all** unique
character leases in the same transaction, rechecking real SQL account,
PERSISTENT enrollment, guild ownership/membership and lack of saved groups.
One invalid member or conflicting GUID rolls back the entire state/journal/lease
batch. After every write, including a false/lost acknowledgement, readback must
match the proposed result before publishing the state. A rejected mutation
moves the committed operation through recovery and cancellation. Repeating an
old write cannot duplicate a journal entry or release somebody else's leases.

The existing M1 cache publishes an all-or-nothing local batch before persisting
intent. Founding/recruitment reservations reject operation-owned characters;
enrollment retirement/cancellation sees the operation lease facts. All operation
participants are already PERSISTENT, so the **existing** maintenance v1 provider
continues to veto randomisation, resets, refresh, random teleport, bot-led group
teardown and population trims. Login handoff also consults the local lease map.
No provider is replaced or weakened, and no extra protection system is installed
in playerbots. Normal autonomous AI may change activity; fresh validation then
cancels the simulation. This is not proof of stable live bot-led groups.

Leases never renew and expire after a bounded absolute deadline. Scheduler elapsed
time also advances the local clock, so a backward wall-clock correction cannot
extend a local lease indefinitely. Completion,
failure and cancellation delete durable leases in the terminal transaction;
local leases release only after terminal readback, or absolute expiry. Expiry
leaves M1 protection intact. If SQL is unavailable, expired local leases cannot
block a character indefinitely; durable rows remain quarantined from reuse
until SQL recovery closes the operation. Restart processes one unfinished
operation at a time: RECOVERING -> CANCELLED, releasing leases without touching
characters, core groups or earned progress. It never resumes an executor.
Shutdown drains queued module SQL, drops local leases and leaves unfinished
intents for this same recovery path. Disable or reload-to-dry-run cancels
existing simulations while continuing acknowledgement/recovery work.

## Read-only operator commands

Console or administrator-only RA (`Ra.MinLevel >= 3`), no in-game sessions:

```
autonomy-operation status
autonomy-operation plan 23
```

For Ashen Vanguard, `plan 23` reports available tank/healer/DPS counts, eligibility,
selected participant GUIDs/roles when complete, and rejection counts including
stale evidence, offline/busy members, ambiguous roles, equipment, faction,
levels, queues, groups, M1/control boundaries and reservation conflicts. It also
reports operation IDs/states/age/outcome, local reserved count, pending writes,
recovery and query/cost counters. The commands never refresh, enqueue or write
anything; enable the explicitly read-only dry-run cache to collect evidence.
No human names/accounts are printed. Only managed bot GUIDs are shown.

## Evidence and remaining gates

Deterministic C++17/C++20 tests cover roles, valid/invalid party construction,
control/M1/faction/level/equipment/activity exclusions, guild preference,
duplicate GUIDs, bounded candidate work, competing atomic local batches and
lifecycle/timeout/cancellation. Disposable SQL tests execute the production
statements, including two simultaneous MySQL connections competing for leases,
CAS/journal rollback, crash-before-commit, replay, invalid account/M1/ownership/
group facts and repeated cleanup.

The binary harness compiles the actual stores and runtime, using the pinned
core's unmodified Field/PreparedResultSet decoder. It covers >2^53 operation IDs,
production parameter bindings, lost/late acknowledgements, active-operation
restart recovery with M2 off, default read-only dry-run, role/spec/queue/control
sampling, finite lease expiry (including backward clock correction), four-operation
concurrency and one-SQL-job-per-tick bounds, M1 maintenance-provider vetoes, command permissions, and all four unchanged
guild identities/founding intents. Its engine objects and async scheduling are
simulated; database transactions and result decoding are real. Existing
founding/recruitment/recovery and 8,250-character churn regressions remain in
the same harness. Real-header syntax checks include all new adapters.

A later batched worldserver build/link, startup validation, live M2 dry-run and
simulation acceptance, map/AI/DB baseline measurements and a 24-hour soak remain
open in #6, #7 and #13. Actual group assembly needs a separately reviewed
executor, legitimate group APIs, protection of live groups and target-specific
access checks. M3 additionally needs authorised dungeon-clear integration.
No image build, production SQL, deployment, restart or automatic merge is
part of this delivery.
