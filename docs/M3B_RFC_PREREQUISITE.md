# M3B RFC entrance/exit audit — blocked source prerequisite

20 November 2026. Review corrections for draft PR #32 and owner dungeon-clear
PR #11. Issue #30 remains open; its previous UK target is superseded by RFC.
No M3B movement implementation or runtime gate is introduced. The requested
stop condition applies: the complete recovery mechanism cannot be verified
in the pinned source set. Do not enable the real pilot on this evidence.

## Verified smallest ordinary portal path

Inspected read-only core `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` and owner
playerbots `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`, including their AGENTS.md.
No stock-core or unpinned module APIs were substituted.

* Core `data/sql/base/db_world/areatrigger_teleport.sql`: trigger **2230** targets
  RFC map 389; **2226** targets outside map 1. These are destination records,
  not authoritative trigger shapes or evidence of a navigable path.
* Playerbots `src/Ai/Base/Actions/AreaTriggerAction.cpp`:
  `ReachAreaTriggerAction::Execute` reads a trigger packet, excludes selfbots,
  checks loaded trigger/teleport/map data, and calls existing path-generating
  `MotionMaster::MovePoint`. No human master supplies authorization to this
  action. `WithinAreaTrigger::IsActive` checks the loaded trigger geometry;
  `AreaTriggerAction::Execute` calls the bot's ordinary session handler.
* Core `src/server/game/Handlers/MiscHandler.cpp`:
  `WorldSession::HandleAreaTriggerOpcode` verifies the actual trigger radius,
  map, flight state, script hooks and `PlayerCannotEnter(..., false)` before
  performing the portal transfer. Calling `PlayerCannotEnter(..., true)` in
  M3A preflight does not replace these real entrance checks. Neither the action
  returning true nor an admitted preflight establishes a successful transfer.
* Existing `MovementAction::MoveTo` can reuse playerbots path search. A future
  slice should be limited to five already-local, living, out-of-combat leased
  participants at the RFC entrance, not general world travel. It must verify
  loaded source/destination/trigger data before issuing any movement, observe
  all five GUIDs in the same normal instance, and stop on failed/partial entry.

No extracted local DBC/mmap set is available. Actual trigger geometry, reachable
entrance and exit paths, and portal acknowledgements remain data/live gates.
No action, packet or movement was executed against a realm during this audit.

## Recovery blockers

1. **Logout while leased has no verified cleanup login.**
   `EnrollmentCache::LoginBlocked` returns true whenever `OperationOwner(guid)`
   is nonzero. `AllowLogin` clears only the enrollment reservation's login flag;
   it does not override an operation lease. Playerbots
   `PlayerbotHolder::AddPlayerBot` and `HandlePlayerBotLoginCallback` both enforce
   `AutonomyEnrollment::LoginBlocked`. The existing enrollment handoff is not
   an operation recovery capability. A missing/offline member cannot satisfy
   CoreGroupPort safe control/cleanup. Releasing the exclusive lease to log in
   would reopen conflicting activities and is not an acceptable workaround.
2. **Normal exit leaves real instance bindings.**
   Core `InstanceMap::AddPlayerToMap` in `Maps/Map.cpp` creates a normal player
   bind (and leader bind) and records ordinary hourly instance entry.
   `CoreGroupPort::Inspect` refuses disband while any member has any binding,
   even after ordinary exit. Core `Group::ForcedDisband` invokes
   `Player::ResetInstances(..., INSTANCE_RESET_GROUP_LEAVE, false)`;
   `Entities/Player/PlayerMisc.cpp` shows that this removes nonpermanent binds
   outside the instance. Thus blindly allowing disband changes saved identity
   and cannot be labelled preservation of instance access/loot restrictions.
   No reviewed operation-owned binding reconciliation policy exists here.

Before implementing M3B, specify a narrow owner-authorized cleanup login that
retains the same durable/local lease and M1 vetoes, and define exact normal-bind
ownership/expiry/cleanup evidence without resetting, unbinding or replacing an
instance to obtain fresh loot. Require thread ownership, idempotency, restart,
dead/stuck/absent member and human-handoff behavior in that review. An ordinary
portal API alone does not close these blockers. No core/playerbots patch, GM
teleport, fake master, provisioning, resurrection or forced unbind was added.

## Failure-containment tests and their limits

The actual core-group fixture checks failed/partial entry (four inside, one
outside), different instance IDs, all-five colocation, and bindings retained
after exit. Cleanup remains refused until injected safe binding absence.

The production scheduler, stores, binary core decoder and DC controller run on
disposable MySQL with injected engine evidence. They cover no-entry timeout,
separated-member timeout, kill-switch cancellation and an executor observation
with the wrong instance. No DC launch occurs without colocation; no scenario
succeeds. Five exclusive leases and M1/login protection remain beyond expiry
while cleanup is unsafe. After injected ordinary recovery/exit, even a successful
disband retains leases until actual core-group SQL absence is confirmed. Existing
restart, logout/control and missing-stop tests remain required.

These are tests of existing failure containment, not tests of an implemented
portal controller, automatic safe exit, actual core admission or recovery login.
M3B and live acceptance remain blocked; manual handoff with retained protection
is not a completed expedition.

## Later combined build and one-party acceptance plan

This is a plan only. Do not initiate a build or live run from this review.

1. After the recovery prerequisites have a separately reviewed implementation,
   freeze a manifest of the immutable consumer/provider PR heads, the two
   dependency SHAs above, intended core branch/toolchain and data/config
   revisions. Recheck actual deployed source provenance; these validation pins
   alone do not prove a branch pairing. Refresh/retest pins if the intended
   `test-staging` stack differs. Never silently switch to stock AC or upstream
   playerbots. Retain existing migrations, compatible stop provider and M1.
2. With separate owner authorization, compile and link the three modules into
   the intended core, verify the generated loader, then start in an isolated
   acceptance environment with every new activity off. Record compiler/C++
   dialect, exact SHAs, loader/API availability and startup/schema evidence.
   Regress ordinary player/GM DC, RDF/BG and filler maintenance. The header
   syntax/contract checks from this review do not satisfy this step.

   Planned commands after that separate authorization, in a fresh external
   build directory with the frozen modules checked out under the core's
   `modules/` directory (C++20 toolchain required by this core):

   ```sh
   cmake -S "$acceptance_core" -B "$acceptance_build" \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSCRIPTS=static -DMODULES=static \
     -DTOOLS_BUILD=none -DBUILD_TESTING=OFF -DWITH_WARNINGS=1
   cmake --build "$acceptance_build" --target worldserver \
     --parallel "$approved_build_jobs"
   ```

   Use the intended environment's reviewed dependencies/module list and record
   all resolved refs and compiler flags. Do not run `install`, publish an image
   or start that binary against production as part of the build. Isolated
   startup and any later realm change need their own approved execution scope.
3. Select **one existing verified managed guild** and **one real operation**
   (`MaxReal=1`, `MaxConcurrent=1`, `CrossGuild=0`), using five naturally eligible
   dedicated persistent bots with tank/healer/three DPS and no human master.
   Do not alter the four guilds, reset progression, provision gear or bypass
   access. If five do not qualify, stop. Snapshot actual progression and guild
   identities privately before/after; use the detailed [M3A runbook](M3A_CLASSIC_PILOT.md).
4. Set matching expansion bounds only from independently verified operator
   knowledge. `RealmMaxExpansion`/`Autonomy.MaxExpansion` are supplied ceilings;
   neither detects realm progression or unlocks. Actual script/portal access
   must independently succeed for every member.
5. Confirm durable group SQL and five leases, ordinary portal entry into one
   nonzero map-389 normal instance, exact permit/run identity and masterless
   reactions. Exercise entry denial, separation, timeout, cancellation, wrong
   instance, wipe/logout and restart only through approved ordinary mechanisms.
   Require bounded stop or actionable protected handoff, never reward success
   for a timeout, skipped boss or simulated state.
6. Within that one operation require all four genuine encounter kills, natural
   `all_cleared`, real rewards, ordinary safe exit and verified binding policy,
   executor quiescence, owned-group cleanup, core SQL absence and five lease
   releases. Stop further admission immediately. Preserve enrollment/receipts
   and M1; verify all four guild identities and unrelated progression unchanged.
   Record world/map/AI timing, bounded scheduler/SQL work and cleanup time.

Record **integration smoke** separately from **at-level combat acceptance**.
A naturally overlevelled party can exercise the bridge, identity, kills and
cleanup, but cannot establish RFC combat balance, healer/tank behavior under
appropriate pressure or ordinary at-level XP. At-level acceptance requires an
already eligible approximately level-15–20 party, combat/reaction evidence,
all four kills and normal earned XP/loot. Never downlevel or supply rewards.
Use whichever category the one available party actually supports and leave the
other unaccepted; repeatability/soak acceptance is outside this one-run limit.
