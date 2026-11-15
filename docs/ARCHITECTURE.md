# Architecture and safety invariants

> Planned runtime design; the foundation module and pure policy rules exist, plus a not-yet-applied enrollment schema and lifecycle contract. No live enrollment or reset guards have been wired.

## Three cooperating modules

```text
mod-playerbot-autonomy            decision / orchestration / persistence
   |                    |
   v                    v
mod-playerbots         mod-dungeon-clear
character AI,          dungeon navigation,
guilds, professions    encounters, driver
   \                    /
    \                  /
       AzerothCore WotLK
```

Both dependent modules and the core must be compatible. This module must not import their implementation or require GM impersonation. Full integration requires small reviewed APIs where existing interfaces are insufficient.

## Population lifecycle

```text
random bot, no guild  -> WORLD_FILLER -> original level rotation/rerolls
         |                                  |
         +---- safe guild recruitment ------+
                      |
                      v
              GUILD_PERSISTENT
              actual XP/gear/money/skills
                      |
           leaves guild / guild disbanded
                      |
                      v
             STILL PERSISTENT
                      |
             explicit audited retirement
                      |
                      v
                 WORLD_FILLER
```

To avoid a crash gap during recruitment, first durably stage a **PENDING** record, then perform the guild operation, then confirm **PERSISTENT**. The pending state prevents destructive maintenance until the operation is safely reconciled. See [ENROLLMENT.md](ENROLLMENT.md).

A character in a human-managed guild can be classified as persistent but MUST NOT automatically be governed by the autonomous guild director. Human-controlled and selfbot characters are unmanaged, irrespective of guild membership.

The runtime classifier must check authoritative random-bot account membership, current human control, guild status and durable enrollment. The current pure function models decisions only; it does not intercept the real reroll paths.

### Hard maintenance gates

Before enabling persistent mode, audit and protect **all** relevant mutation paths, including:

- `RandomPlayerbotMgr::ProcessBot` (disbands non-LFG groups led by random bots, randomises and teleports idle bots).
- `RandomPlayerbotMgr::Randomize`, `RandomizeFirst`, `IncreaseLevel`, `Refresh` (randomise, repair, money and group side effects).
- `RandomBotLevelMgr` bracket adjustment, level reset/skip and pending reset paths.
- Login, logout, periodic population trimming, class/spec reinitialisation, gear regeneration and guild changes.
- Existing profession loop, auction posting and guild-bank deposit action must keep working in both populations.

Persistent bots must not gain fabricated items/gold from maintenance. Unguilded bots must remain untouched when autonomy is off.

## Future runtime structure

- `AutonomyDirector`: central, rate-limited world-thread scheduler; registered work and activity leases.
- `PopulationManager`: durable enrollment, classification, recruitment and explicit retirement.
- `GuildDirector`: bot-owned guild rosters, actual capabilities, goals and resource limits.
- `OperationCoordinator`: one generic roster/lease/state machine for five-player dungeon runs, outdoor groups, battleground-style operations and 10-/25-player raids.
- `DungeonClearAdapter`: authorised internal integration for start/status/stop; **never** spoof user chat events or modify GM command checks.
- `EconomyDirector`: planning and coordination using existing mod-playerbots crafting, vendor, auction, mailbox and guild-bank mechanics.
- `Diagnostics`: concise admin visibility, reason codes, performance counters, watchdog and recovery telemetry.

Do not create these as empty directories/classes; each should arrive with a tested behaviour.

## Generic operation lifecycle

```text
proposed -> validated -> reserved -> assembling -> active -> closing
                     \         |         |         /
                      \--------> recovering ------/
                                    |
                          released / terminal
```

Every stage is idempotent and time-bounded. Store IDs and intentional state, not `Player*`, `Group*` or `Map*` across restarts. Re-read actual guild, player, session, group and instance state during recovery. Leases must be released exactly once (idempotently), including in disabled mode, on restart, after a failed teleport or wipe, and when the underlying module disappears.

**Operation type** configures roster size, capabilities and executor; it does not fork a whole scheduler implementation. A dungeon may require tank/healer/3 DPS; a 25-player raid requires separate validation and encounter support. Valid groups should use stock AzerothCore group/raid logic.

## Safety and permissions

- Never change, recruit or direct real players, active selfbots or human-owned guilds by default.
- Independent, explicit capability/permission checks for guild-directed actions (membership alone is insufficient).
- Normal dungeon access, group and instance locks, loot rules, PvP flags, faction and guard mechanics apply.
- For future open-world PvP, use opt-in server configuration, permitted zones, cooldowns, concurrency caps and anti-harassment protection for real players and starter areas.
- For economy, enforce supply ownership, actual vendor stock and currency budgets; avoid bot-market feedback loops and duplicated loot.
- A single kill switch must stop *starting* new work, and allow safe cancellation/cleanup of existing activities.

## Performance and release gates

- No O(N) scan on every per-bot AI tick; central incremental work queues and bounded budgets.
- No synchronous database I/O or blocking cross-thread calls on map threads.
- Separate observations from decisions; pure decision functions should compile/test without the whole game server.
- Measure map/world ticks, AI delays, DB queries, active bot count, concurrent operations and recovery.
- Features start disabled; require a live test on the intended core and exact fork commits before rollout.
- Protect existing RDF fill, GM dungeon-clear test runs and existing auction/profession workflows from regressions.
