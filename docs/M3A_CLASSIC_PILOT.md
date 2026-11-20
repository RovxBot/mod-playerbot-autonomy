# M3A Classic RFC pilot — source candidate, 20 November 2026

This supersedes the UK target in [the historical M3A release](M3A_RELEASE.md).
It is not a real dungeon clear or production acceptance. Issue #9 remains open;
travel, entrance/exit automation and binding-safe recovery stay in #30 (M3B).
M1 enrollment/progression protection and the four managed guilds are unchanged.

## Compatible source set

| Component | Exact validation revision |
| --- | --- |
| [Dungeon-clear v2 provider PR](https://github.com/RovxBot/mod-dungeon-clear/pull/11) | `fad8a7691197f5be922f00d6cd2d3430e68dc48c` |
| Playerbots (no patch) | `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59` |
| Existing core test-staging header snapshot (read-only) | `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` |
| Autonomy consumer base | `a5630a66956448fad0e44ac969673eafeac2f817` (use the immutable head recorded on this PR) |

These pins are not the resolved source manifest of the deployed realm. The
provider is based on merged owner master `565086c`; no other repository is
modified. Review the provider before the consumer; use both exact heads in a
later combined build. The v2 ABI uses a separate inline namespace so older
compiled v1 providers cannot interpret its new permit layout. A missing/old
header set leaves real profile admission unavailable; an unregistered or
wrong-version executor cannot launch. M2 simulations remain available.

## What changed and why RFC is suitable

The shared versioned profile lives in the owner provider's
`src/Integration/DcAutonomyPilot.h`: profile version 1/id 1, map 389, normal,
Classic content, minimum level 15 and equipped average 13. Real participants
must be within Classic level 60 or TBC level 70; a Wrath cap is refused even if
configured. These are conservative admission floors, not encounter balance
measurements. Existing equipment/spec/role freshness and main-hand/broken-item
checks still apply. Generic configured bounds may further restrict the roster.

Planning, actual core admission, each follower addition, owner validation and
provider validation consume the profile. No UT/level-70/item-level-115 constants
remain in the production execution path. Only one existing verified MANAGED
guild is eligible, selected with `PilotGuild`; no guild is created or renamed.
Normal `PlayerCannotEnter` checks include script hooks, progression requirements
and instance count. No attunements, keys, gear, XP, currency, loot, skills or
levels are granted and no access data is edited.

The [provider audit](https://github.com/RovxBot/mod-dungeon-clear/blob/fad8a7691197f5be922f00d6cd2d3430e68dc48c/AUTONOMY_CLASSIC.md)
records the actual source. Four kill-credit definitions match normal static
spawns: Oggleflint (430/11517), Taragaman (431/11520), Jergosh (432/11518),
Bazzalan (433/11519). DC discovers these through its existing DBC/SQL/spawn
join, follows loaded encounter order using the ordinary next-boss policy and
executes its normal navigation, trash and boss actions. RFC already has a
tested funnel-wall nav penalty. No encounter-specific event/wing override is
present or newly needed by this source; Stockade fallback is not selected.

RFC's instance script has no internal boss-state slots. At launch the provider
requires exactly the four expected kill definitions, distinct bit indices below
32 and matching DC normal anchors. It derives the mask from loaded game data,
then requires a fresh instance with none of those bits set. Core KillRewarder
and Map::UpdateEncounterState set the instance completion bits after real kills.
Completion needs all four required bits **and** normal DC `all_cleared`, with
the exact operation/run/leader/group/map/instance identity. Taragaman's RDF
last-encounter marker, a corpse, entry, a skipped boss, objectives or a generic
disabled state cannot establish a clear. The old UK mask 7 fails.

No local extracted DBC/mmap set was available for an actual RFC run. Runtime
definition/spawn validation refuses missing/incompatible data; it does not
replace checking the deployed data or proving navigation in game.

## Operator runbook for one later live acceptance test

Do not execute this runbook as part of source review. First require review,
a separately authorised combined compile/link/startup on the intended realm
source set, existing migrations and confirmed M1 protection. No new migration
is needed here. Preserve all M1/M2/execution tables and receipts.

1. Choose one of the existing four MANAGED guilds. Read `autonomy-operation
   status` and `autonomy-operation plan <guild-id>` through the console or
   administrator-only RA. Record the four guild IDs/names/leaders, M1 state,
   five selected GUIDs/accounts and exact source/config manifest. Readiness must
   be established using their real gear, talents and levels. Prefer a suitably
   levelled RFC party (for example 15–20); never lower/reset a higher character
   or provision a replacement to make the plan pass. Use one tank, one healer,
   three DPS, all dedicated persistent bots with no human master, queues,
   invite, old group or instance binding. If no eligible five exist, stop.
2. Take read-only before snapshots of each character's level/XP, money,
   gear/durability/inventory, talents/skills/professions, quest and achievement
   state. Record current guild membership and enrollment. Preserve these
   privately; do not commit production data. Confirm ordinary RFC access under
   the realm's TBC progression rules and ensure fresh encounter definitions and
   the four required normal spawns are loaded. Stop on a provider/data mismatch.
3. Legitimately position the five **outside** the RFC portal before assembly.
   The adapter refuses to assemble inside any dungeon because group joining
   can reset binds. After assembly, all five must cross the ordinary portal
   into the same normal instance using a verified ordinary movement mechanism.
   This patch supplies no travel/portal command. Arrange and verify that
   positioning/entry mechanism separately before enabling real admission; if
   unavailable, stop the pilot. Do not use GM teleport, unlock/provisioning,
   human-master impersonation, manual group replacement or the `.dc test`
   harness. Merely injecting an inside-instance state is source-test evidence.
4. In that later approved test environment, start with read-only planning
   (`Operations.Enable=1`, `AssembleGroups=1`, `DryRun=1`) and the existing M1
   activity switch. DryRun prevents group creation while the real profile is
   applied to the plan; verify that scoped plan before permitting execution.
   Set this additional scope in the reviewed configuration:

   ```ini
   PlayerbotAutonomy.Operations.PilotProfile = 1
   PlayerbotAutonomy.Operations.PilotGuild = <chosen-existing-managed-guild-id>
   PlayerbotAutonomy.Operations.RealmMaxExpansion = 1
   DungeonClear.Autonomy.MaxExpansion = 1
   PlayerbotAutonomy.Operations.MaxReal = 1
   PlayerbotAutonomy.Operations.MaxConcurrent = 1
   PlayerbotAutonomy.Operations.CrossGuild = 0
   PlayerbotAutonomy.Operations.AssemblySeconds = 300
   PlayerbotAutonomy.Operations.CooldownSeconds = 3600
   ```

   Then explicitly authorise `DryRun=0`, `TrustedDungeon=1`
   and `DungeonClear.Autonomy.Enable=1`. Every shipped activity/profile gate
   remains off by default. This is a single pilot; do not enable recruitment,
   founding changes or any other dungeon/population expansion for it.
5. Observe an ordinary five-member tank-led group, durable `group_sql_confirmed=1`
   and all five leases before launch. After normal entry verify map 389, one
   nonzero instance ID and normal difficulty for all five. Read-only status must
   show one DC operation/run and `running`/`paused` before real `ACTIVE`; there
   is no manual mutating start command. If group/instance/identity, access or
   provider evidence fails, stop admission and follow the cleanup table below.
6. Record all four real kills and their core completion bits in that instance.
   `dc_required_encounters` is resolved from actual definitions; it need not be
   numeric 15 on custom data. Require `dc_encounters` to contain all four bits,
   the exact run identity and a natural `all_cleared` terminal reason. No skip
   or generic state transition is acceptance. Observe actual masterless reaction
   timing and map/world tick load. A stuck/wiped run is a failed pilot, never a
   reason to reset the instance or award success.
7. On terminal evidence, set `Operations.KillSwitch=1` (or disable activities)
   to stop subsequent admission. Keep M1 protection running. Arrange an ordinary
   safe exit for all members; DC stop alone does not move them out. Cleanup must
   acknowledge the exact stopped executor, disband only the unchanged owned
   group outside combat/instances/binds, confirm core group SQL absence and
   release all five operation leases before terminal operation cleanup. Record
   status and private after snapshots; prove earned progress/rewards persisted
   with no reroll/provisioning. Confirm all four guild identities unchanged.
   Revert the pilot gates to off and keep the protection-capable binary/tables.

## Cleanup limits requiring manual intervention

| Evidence | Required action |
| --- | --- |
| Dead, wiped, stuck, fighting, inside or still bound | Stop admission; arrange normal recovery/exit and lawful bind expiry. No automatic recovery teleport/unbind or destructive disband. |
| Logout or owner lost | Keep leases/protection. No cleanup-login exception is provided; lease-vetoed login may require separately reviewed operator recovery. |
| Human control, foreign member/leader, changed guild ownership | Treat as handoff. Do not command/disband; explicitly reconcile ownership using ordinary approved administration. |
| Restart with an existing group or uncertain creation | Never adopt/resume. Operator must reconcile actual group/DB/executor absence; durable evidence, not guessed ownership, permits release. |
| Provider disappears, stop unacknowledged, SQL delayed/unavailable | Hold protection past admission deadlines and restore evidence/provider availability. Do not delete receipts/leases to make status look clean. |

Natural DC completion and successful cleanup are separate observations. A clear
with retained leases is **cleanup pending**, not a completed acceptance test.
Do not downgrade either bridge binary while a real operation or uncertain
executor remains; retain the compatible stop provider, M1 protection and all
ownership receipts until confirmed cleanup.
Run ordinary player-led DC, GM harness, RDF and BG regression checks separately
after startup; their unchanged source authorization is not gameplay evidence.

## Verification record

The [verification record](M3A_CLASSIC_EVIDENCE.md) records the final commands/results: all autonomy standalone suites and
the provider controller/authorization/actual encounter-adapter bodies under
C++17/C++20; Python SQL tests and binary production-store/controller harnesses
on disposable MySQL; pinned core header syntax and source contracts. Engine
group, instance, kills and portal/exit evidence in those tests are fakes. No
combined worldserver build, deployment, Kubernetes/configuration change,
production database access/write or live dungeon run was performed.
