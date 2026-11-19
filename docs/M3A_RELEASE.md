# M3A source candidate — 19 November 2026

This is review/build preparation, **not production acceptance or an autonomous
dungeon clear**. The accepted M1/four-guild baseline is unchanged. No realm
build, deployment, restart, production migration, character/guild mutation or
infrastructure edit was performed. Issue #9 stays open.

## Source set and merge order

1. Merge the owner [dungeon-clear companion](https://github.com/RovxBot/mod-dungeon-clear/pull/10):
   **`f9201249e287027f0d393da17eb85ef588c49847`**, based on owner master
   `7f1600bacfc282a8b9407a4089407747bdce1480`.
2. Merge this autonomy consumer, based on merged main
   **`f2f27cbc1b4a79d076ae261973bdf6a0db043aaf`**. The consumer PR records its
   immutable head SHA; build that head with the exact companion above.
3. Keep playerbots at owner master **`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`**
   and the supported core test-staging snapshot
   **`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`** used for header validation.
   No playerbots or core changes are included.

Ancestry checks confirmed the previous dungeon-clear contract pin `b1be76d...`
is an ancestor of the provider base, and the M1 handoff commit `422db5a...` is an
ancestor of playerbots `696a009...`. Older ordinary local checkouts were not
silently mixed into validation. The historical image workflow did not publish
resolved dependency SHAs: these pins do **not** claim to identify the exact
deployed binaries. See [the audit](M3A_EVIDENCE.md).

## Implemented boundary

The existing M2 scheduler chooses the roster, persists an intent and acquires
exclusive local/SQL leases. Real mode fences creation durably, revalidates five
dedicated M1 PERSISTENT bots in a verified MANAGED guild, and calls normal
`Group::Create`, `GroupMgr::AddGroup` and `Group::AddMember`. The selected tank
leads; one healer and three DPS remain mandatory. Human sessions, selfbots,
client masters, queues, groups, invites, old bindings, incompatible difficulty
and stale evidence fail closed. Cross-guild real groups are refused.

Core APIs queue their group SQL. After authoritative member-slot confirmation,
the module asynchronously verifies actual core group/character rows and
persists `group_confirmed` through the same CAS/journal/readback protocol.
Only then may it issue a trusted launch permit. Cleanup requires the exact
owned group, safe live members, an acknowledged stopped executor, authoritative
group absence **and asynchronous core SQL absence** before terminal lease
release. Late creation/deletion writes cannot be treated as completed work.
Readbacks use group primary/leader indexes and the five member primary keys;
they do not scan the realm. Production code never writes core group SQL.

Group hooks taint ownership on external membership/leadership changes, even if
someone changes it back between scheduler passes. Human control or joining is
a cancellation/manual-handoff outcome, never permission to disband. A lost
receipt is reconciled from the in-process creation identity; an unknown restart
group is never adopted. Cleanup is idempotent.

Real `ACTIVE` requires a DC executor acknowledgement. Simulated `ACTIVE` retains
its old meaning and is explicitly labelled `SIMULATED`. Permits are private to
the registered owner, expire after ten seconds, are consumed once, and bind
owner/operation/revision, all five GUIDs, tank, group and instance. The owner
revalidates M1/control/leases; arbitrary IDs are never credentials. Stop/status
refer to the exact run and preserve real terminal evidence. Completion requires
the normal all-cleared signal **and all three real boss completion bits**;
skipped bosses alone cannot report success.

Manual DC authorization is unchanged. Shared initialization reuses strategy,
wing, boss, pull and navigation resets. Scoped AoE avoidance is added for the
five participants and restored only while ownership remains safe. Autonomous
stranded/wipe/breath recovery teleports, breath-item grants, combat purge and
pull daze immunity are suppressed. No test provisioning, fake Event owner, GM
master, teleport, key, quest, achievement, equipment or currency grant is used.

## Target and unresolved runtime limits

Utgarde Keep normal (574) has three ordered forge-clear objectives and the three
DBC encounters Keleseth, Skarvald/Dalronn and Ingvar. Its curated DC route needs
no quest/attunement grant. The pinned normal access template has minimum level
65 and no minimum item level; the pilot conservatively requires level 70 and
equipped average 115 **in addition to** fresh role/equipment evidence and the
actual core access check. These are admission floors, not proof a party can
win. Heroic/other maps are unavailable. No access data is modified.

The [M3B prerequisite](https://github.com/RovxBot/mod-playerbot-autonomy/issues/30)
must address these real limits:

- No autonomous travel, entry or exit is implemented. Launch requires lawful,
  already eligible co-location in the exact normal instance. The combined fake
  test injects that state; it does not move characters.
- Core group changes reset/unbind instance state. Assembly rejects existing
  bindings; cleanup waits while anyone is in an instance, fighting, absent,
  dead or still bound. Normal exit/binding handling needs separate design.
- Existing restart groups and externally changed groups require manual
  ownership reconciliation/ordinary cleanup. Protection remains held; no stale
  run resumes. An absent member may need a reviewed cleanup-login path because
  operation leases currently veto login.
- Masterless reaction delay remains the playerbots policy
  (`PlayerbotAI::GetReactDelay`, roughly 250–500 ms in combat and potentially
  seconds out of combat under relevant configuration). No scoped public
  override was verified. Fast timing is an explicit integration/pilot gate;
  no human-master impersonation is substituted.

Missing acknowledgements, provider loss or unsafe ownership retain real leases
beyond their absolute **admission** deadline and report the pending reason.
This is a protected manual-intervention condition, not successful cleanup.
Feature disable stops admission and continues reconciliation without discarding
unsafe work. Cloud9 cluster group lifecycle is unsupported and rejected. Scope
remains the current four guilds; #29 is separate.

Specific source evidence for the remaining gates (line numbers at this candidate):

| Gate | Evidence |
| --- | --- |
| Co-location without travel | `src/Operations/OperationRuntime.cpp:672` only launches when the observed group is already in map 574 with an instance ID. |
| Existing bindings / exit cleanup | `src/Operations/CoreGroupPort.cpp:82` rejects prior binds; `:140` retains cleanup while inside/bound/unavailable. |
| Restart ownership | `src/Operations/OperationRuntime.cpp:557` explicitly reports manual reconciliation instead of taking over an existing group. |
| Cleanup login | `src/Persistence/EnrollmentCache.h:151` still vetoes operation-owned logins; no new trusted login exception is claimed. |
| Reaction timing | Owner playerbots `696a009...`, `src/Bot/PlayerbotAI.cpp:6906` retains `GetReactDelay`; no compatibility patch is included. |

## Configuration and diagnostics

All distributed controls stay off/read-only by default:

| Control | Meaning |
| --- | --- |
| `Operations.Enable=1`, `DryRun=1` | Read-only roster/plan sampling |
| `DryRun=0`, `AssembleGroups=0` | Existing M2 simulations |
| `DryRun=0`, `AssembleGroups=1` | Separate real assembly gate |
| `Operations.TrustedDungeon=1` and `DungeonClear.Autonomy.Enable=1` | Optional trusted launch |
| `Operations.MaxReal=1` | Exactly one pilot real operation; other values rejected |
| `AssemblySeconds`, `RunSeconds`, `ProgressSeconds` | Assembly/co-location, admission and actual no-progress budgets |
| `CooldownSeconds` | Delay after terminal cleanup; no automatic creation/start retries |
| `Operations.KillSwitch=1` | Stop admission and reconcile existing ownership |

The master activity switch and M1 protection remain required. Read-only
`autonomy-operation status` / `plan <guild>` retain console/administrator-RA
permissions. They report mode, state, group/creation/SQL evidence, held leases,
DC run/map/instance/reason, cleanup decision, query/write counts and scheduler
microseconds. They never enqueue/change state. No new mutating command exists.

## Tests actually executed

The provider pin was advanced during review to restore the missing Nightbane
intro hook (66), which caused the Karazhan event-shape and custom-hook registry
regressions. The single-file correction passed GNU C++20 real-header syntax,
C++17/C++20 checks of the actual hook body/registration with engine fakes, and
the bridge/authorization and lint checks. The initial SQL/controller evidence
below predates this correction; bridge sources are unchanged. Refreshed CI
uses the corrected provider pin; the full gtest rerun is recorded on the PR.

- Existing population, enrollment, cache, transition, founding, naming,
  recruitment and operation suites under C++17/C++20; new real-executor and
  **actual `CoreGroupPort.cpp`** tests with fake engine APIs.
- Production DC controller and extracted unchanged chat authorization under
  C++17/C++20: permits, replay/mismatches, lifecycle/stop, permissions and real
  terminal policy. These do not execute full game initialization.
- **58 Python tests, no skips**, on disposable MySQL **8.0.44** and **8.4.10**:
  schema replay, one-time M2 receipt migration, missing-receipt fail-closed,
  transactions/CAS/journals, competing leases and rollback.
- Production stores/runtime and matched DC controller with the unmodified
  pinned core binary decoder on both MySQL versions: lost module ACK, delayed
  core creation/deletion writes, durable roster receipt, no simulated real
  `ACTIVE`, actual owner callback, single launch, terminal/stop evidence,
  unsafe-expiry protection, master-off cleanup and protected half-group restart.
  Engine group/instance/run/exit evidence is fake; SQL/bindings/decoder and both
  controllers are real. A separate missing-DC variant was exercised.
- **24 GNU C++20 real-header syntax checks**, source contracts, scaffold, core
  guild-name checks and DC config/portability/determinism checks. No worldserver
  object build/link/startup or actual core groups were executed.

Local samples: 10,000 fake group lifecycles took roughly 7–8 ms; 100,000 fake
bridge ticks roughly 2–3 ms. The delayed-SQL fixture scheduled at most one job
per tick, one creation/four additions/one disband, 28 read jobs and eight module
transaction jobs (each contains several statements); five failed ACKs were injected. Sample max scheduler duration was below
0.6 ms. These small-fixture numbers are **not realm load, map latency, production
SQL cost or a soak result**. Memory is bounded by existing roster/operation caps,
one DC run/permit and active group receipts; terminal entries leave local maps.

Full compile/link/startup, manual DC/GM/RDF/BG gameplay regressions, lawful
dungeon/reward retention, real-instance fault recovery and the #13 soak remain
unexecuted. [LICENSING.md](LICENSING.md) retains the prior unresolved review;
no distribution-license decision or implementation import is made here.

## Migration, rollout and rollback

Keep gates off for review. In the later combined build, ship existing M1/M2
migrations followed by `2026_11_19_02_playerbot_autonomy_execution.sql` through
the normal module character updater. It creates module receipts, backfills old
simulations once and adds an ID-zero schema receipt. Replay never invents
simulated evidence for a later missing real receipt. No core character, guild,
progression, world or playerbots table changes. The provider has no migration.
The consumer compiles without DC; launch fails closed and M1/guilds remain.

Use `KillSwitch=1` or disable activity gates for cancellation. Keep the new
protection-capable binary and all enrollment/operation/receipt tables until real
groups, executor work, SQL readbacks and leases are conclusively closed. Do not
downgrade to an older M2 binary while a real operation is open: it cannot
interpret this protocol. Preserve M1 on rollback. Review the later build/pilot
evidence before enabling one guild; do not expand population or alter the four
existing guilds.
