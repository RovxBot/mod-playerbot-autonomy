# M1 maintenance audit — issue #4, first pass

**Status: source audit and pure policy only; NO runtime protection installed.**
This document is a work-in-progress call-site inventory to support a small,
opt-in, backward-compatible integration into the player's mod-playerbots fork.
Do not enable persistence or close issue #4 on the basis of this document or
the tests in this repository.

Reviewed source revision:
[RovxBot/mod-playerbots @ 59e5f26d417fca173d969cd6a1064ec9638b1afa](https://github.com/RovxBot/mod-playerbots/tree/59e5f26d417fca173d969cd6a1064ec9638b1afa).
The observations below apply to **that** revision. Re-run the audit if the
playerbots fork changes before the integration patch lands.

## 1. Findings that change the design

**A guild ID alone is insufficient as a runtime policy.**

1. A guildless random bot is automatically offered membership in a bot guild
   on login through
   [RandomPlayerbotMgr::OnBotLoginInternal (3061–3090)](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L3061-L3090)
   and
   [PlayerbotFactory::InitGuild (5151+)](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L5151-L5182).
   If guild membership automatically made **every** bot persistent, this
   existing login routine could drain the desired unguilded filler population.
   Once enabled, the old automatic placement should be **suppressed** and
   guild recruitment must be an explicit, durable director action. The module
   remains inert until that replacement is safe.
2. [PlayerbotGuildMgr::IsRealGuild](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Mgr/Guild/PlayerbotGuildMgr.cpp#L266-L294)
   classifies using the **leader's account**, not an authoritative census of
   all guild members. Human membership must be checked separately before
   giving a director guild-level authority. A random bot in a human guild
   can be *progression-protected* without being *autonomously commanded*.
3. [RandomPlayerbotMgr::IsRandomBot](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2619-L2642)
   requires account membership **and** that the character's GUID appear in
   the active \`currentBots\` set. A persistent identity must survive logout,
   population downscaling and lost \`add\` events. Never derive the durable
   enrollment flag from \`IsRandomBot\` alone.
4. [PlayerbotFactory::Randomize](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L826-L920)
   can reset talents, skills, spells, quests and inventory; even the
   *incremental* path calls \`ClearInventory()\`, changes related state and
   ultimately generates money and saves the character
   [around 1080](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L1078-L1090).
   \`EquipAndSpecPersistence\` does **not** protect real character progression.
5. [PlayerbotFactory::Refresh](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L1091-L1128)
   clears/replenishes inventory, modifies skills/talents, repairs durability
   and raises money. It cannot safely be run against a persistent character
   as routine maintenance.
6. [RandomPlayerbotMgr::OnBotLoginInternal](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L3061-L3090)
   enforces \`RandomBotFixedLevel\` using \`PLAYER_FLAGS_NO_XP_GAIN\`. The
   eventual persistent path must reconcile/remove that flag if earned XP is
   enabled for guild members. Do not override realm-wide levelling rules by
   accident.

## 2. Concrete mutation and interference map

**All rows below are currently UNGUARDED BY AUTONOMY.** The "intended guard"
column is a proposed integration point, not a claim of protection.

| Code location / entry point | Verified effect or risk | Intended guard before side effects |
| --- | --- | --- |
| [RandomPlayerbotMgr::ProcessBot(uint32), 1682–1776](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L1682-L1776) | Expires \`add\` event, logs out bot, schedules randomize/teleport and processes idle bot | Protect leased activity; ensure persistent enrollment is not tied to \`add\` |
| [ProcessBot(Player*), 1778–1896](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L1778-L1896) | Disbands non-LFG bot-led groups; calls \`Randomize\`, \`Refresh\` and random teleport | Check persistent/lease before each conflicting action; do **not** skip useful profession/auction work |
| [Randomize, 2377–2397](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2377-L2397) | Incremental or full factory reroll | Guard every direct invocation |
| [IncreaseLevel, 2399–2421](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2399-L2421) | Factory-generated level, equipment and skills | Suppress generated levels; **allow natural quest/kill XP** |
| [RandomizeFirst/RandomizeMin, 2422–2536](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2422-L2536) | Full reroll, group teardown and teleport | Check before factory, event writes, AI/group side effects |
| [Clear, 2537–2542](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2537-L2542) | Calls \`ClearEverything\` | Never allow for persistent bot without an explicit future retirement/authorization |
| [Refresh, 2572–2617](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2572-L2617) | Repair, full heal, resource/gold mint, strategy reset, factory refill, group teardown | Deny routine refresh for persistent or leased bot |
| [Revive, 1899–1909](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L1899-L1909) | Calls refresh **then** remote grind teleport | Block combination; later provide genuine resurrection/corpse-run policy |
| [RandomTeleport + level/grind/RPG/strategy path, 1911–2055, 2124–2375, 3783–3818](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L1911-L2055) | Can relocate, reset AI and even change homebind; some callers also refresh | Guard every ingress that can reach teleport; ordinary in-world travel stays available |
| [Factory::Randomize, 792–1090](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L792-L1090) | Real level, XP, spells, skills, talents, quests, equipment, gold and DB save | Defense-in-depth opt-in factory guard in addition to manager entry guards |
| [Factory::Refresh, 1091–1129](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L1091-L1129) | Replenishment, free repair, money and skills | Guard before effect; do not accidentally block legitimate crafted consumables |
| [Factory::ClearEverything, 1653–1666](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L1653-L1666) | Returns character to starting level and wipes progress | Explicit retirement required; preserve earned data until then |
| [RandomBotLevelMgr::GetOrFlagPlayerBracket, 389–477](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomBotLevelMgr.cpp#L389-L477) | Queues out-of-bracket random bots | Exclude persistent from census/rebalance and pending queue |
| [RedistributeSurplusBots / RunLevelBracketsDistribution, 480–709](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomBotLevelMgr.cpp#L480-L709) | Queues and rebalances selected bots, changes actual bracket distribution | Exclude persistent early; recalculate only filler demand |
| [ProcessPendingLevelResets / AdjustBotToRange, 714–802, 294–365](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomBotLevelMgr.cpp#L714-L802) | Applies full factory randomization from deferred queue | Recheck cached enrollment at **execution**, not only when queued; drop stale requests |
| [ResetBot / SkipBotLevel, 829–870](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomBotLevelMgr.cpp#L829-L870) | Full factory reroll to reset/skip level | Guard immediately before factory call |
| [RunResetPlayedTimeCheck / OnBotLogin / OnBotLevelChanged, 875–1070](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomBotLevelMgr.cpp#L875-L1070) | Time-played chance, login immediate reset, automatic natural-level-up reset | Guard at **each** triggering path and at ResetBot |
| [IsRemovableBot / RemoveRandomBots, 905–1029](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L905-L1029) | Trims by logging out eligible idle random bots | Protect exclusive operations; reserve stable guild capacity and persist independently of login |
| [UpdateAIInternal, 330–474](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L330-L474) | \`DisabledWithoutRealPlayer\` can call \`LogoutAllBots\` | Establish no-human population behaviour separately from progression durability |
| [OnBotLoginInternal, 3061–3090](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L3061-L3090) | Calls InitGuild and enforces fixed-level no-XP flag | Suppress legacy random guild placement when autonomy owns recruitment; make guild XP policy explicit |
| [Factory::InitGuild, 5151–5182](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/Factory/PlayerbotFactory.cpp#L5151-L5182) | Automatically creates/joins guild; can grant a tabard | Separate trusted recruitment and preserve existing gear/XP/gold |
| [Console command handlers, 2881–2970](https://github.com/RovxBot/mod-playerbots/blob/59e5f26d417fca173d969cd6a1064ec9638b1afa/src/Bot/RandomPlayerbotMgr.cpp#L2881-L2970) | \`rndbot init/clear/refresh/level/teleport\`; \`rndbot reset\` clears random-bot event records | Persistent safeguards must cover direct manager/factory calls; event reset must **not** erase durable enrollment |

## 3. Existing checks are insufficient

- \`LevelBracketsIgnoreGuildWithRealPlayers\` and
  \`ResetBotLevelIgnoreGuildWithRealPlayers\` only exclude selected
  "real guild" cases. They do **not** provide protection to bot guilds.
- \`IsBotSafeForLevelReset\` checks combat, instances, LFG and some player
  groups, **not** persistence or all future autonomy leases.
- The bracket pending queue rechecks exclusions just before execution,
  but that exclusion does not include autonomy state yet.
- The random manager's normal group protection does not cover all
  bot-led non-LFG operations, and other entry points can disband or
  teleport without going through \`ProcessBot(Player*)\`.
- An explicit console command can bypass a high-level "routine idle
  bot" guard if the underlying mutator is unprotected.
- Existing \`EquipAndSpecPersistence\` is partial equipment/spec policy,
  **not** a durable character lifecycle guarantee.

## 4. Proposed narrow integration contract (not yet installed)

The entry points above should consume a **read-only, opt-in veto**:

\`ShouldBlockRoutineMaintenance(characterGuid, action) -> bool\`

The contract must be implemented through a safe optional registration/
capability mechanism in mod-playerbots. When no provider is registered,
or \`PlayerbotAutonomy.Enable=0\`, existing mod-playerbots behaviour must
remain bit-for-bit semantically unchanged. In particular:

- **False means "no autonomy objection"**, not a new permission to act.
  Existing human-player, LFG, map, guild and safety guards still apply.
- **True means do not perform the disruptive action**. It must be checked
  before any state mutation. No partial reroll then rollback.
- The provider reads a cache/reconciled durable enrollment from issue #5
  (plus an exclusive-lease snapshot when issue #7 exists); never issue
  synchronous SQL queries on per-bot/map thread.
- The controller loads durable enrollment before enabling destructive
  maintenance checks and before any login-triggered reset. If that cannot
  be guaranteed, fail **closed for potential persistent candidates**
  instead of classifying an unknown character as filler and wiping it.
- The candidate may leave a guild during a job: the durable flag remains
  until an explicit, audited retirement. Do not use current guild ID as
  the sole proof of durable enrollment.
- A leased filler bot must not be relocated, disbanded or deleted by
  random maintenance. Normal gathering, crafting, auctioning, quests and
  loot should not be blocked globally.
- Suppressing the **legacy automatic guild placement** is a separate
  onboarding gate; the future GuildDirector will use an explicitly
  authorised recruitment operation. Keep \`PlayerbotAutonomy.Enable=0\`
  and the legacy path intact until that transition is ready.
- Startup/disable/reload policy, callback registration ordering and
  unknown-identity handling must be tested, not assumed.

\`src/Policy/MaintenancePolicy.h\` is currently an **engine-independent
specification**, and its C++17/C++20 tests only validate the intended
decision table. It does **not** hook, protect, enroll, persist or redirect
any live bot.

## 5. Test matrix to complete before claiming protection

| Scenario | Required evidence |
| --- | --- |
| Feature disabled; guilded, unguilded, human-controlled | Legacy random-bot behaviour unchanged |
| Ordinary unguilded filler | Level bracket move, scheduled reroll/refresh, idle teleport continue |
| Guilded bot with genuine gear, XP, money, recipes | Multiple random-manager cycles cause no protected mutation |
| Persistent bot leaving/transferring/disbanded guild | Remains persistent through restart; no reset |
| Guilded bot at max level and natural level-up | No login, time-played, bracket or level-up reset; natural XP works |
| Guilded bot with expired event timers | No repeated invalidation, random money mint or stuck high-CPU loop |
| Active leased bot-led party | No random disband, teleport, pop trim or guild reassignment; cleans on abort |
| Human real player/selfbot and bot in human-managed guild | Never commanded by director; progression protection is independent of guild ownership |
| Manual \`rndbot clear/init/refresh/reset\` | No destructive bypass of persistence; explicit retirement only |
| No real players online / population count changed | Remains safe, no orphaned leases, target population budget accounted for |

For performance, measure the difference in cost between legacy and
registered policy checks. No database lookups, guild member enumeration or
realm sweeps may run per-bot action. Observability should record aggregated
skip counters, not one INFO line per skipped bot.

## 6. Scope remaining in issue #4

This first pass audits the highest-risk *known entry points* in the two
random managers and factory. A complete integration still requires a
cross-file call-site search for additional factory/randomize/teleport
callers, verification of thread affinity, and an actual server regression
with both populations. **The audit matrix is not yet an exhaustive
guarantee**.

The implementation sequence should be:

1. Implement issue #5's durable enrollment identity/cache and explicit
   lifecycle semantics, or a safe restricted precursor that cannot cause
   an irreversible reset if state is unknown.
2. Add small backward-compatible read-only veto points in mod-playerbots,
   with independent tests in that repository.
3. Register an optional, disabled-by-default provider in this module
   only when startup state and ordering are sound.
4. Verify every row in this matrix with controlled real characters and
   repeated manager cycles, then perform a full worldserver build and
   restart validation.
5. Keep issue #4 open until all mutation boundaries and regression gates
   are genuinely covered.

## 7. Immediate safety status

The deployed module at this stage is **foundation-only** and inert.
Neither the audit nor the pure tests modify a live character, playerbot
population, guild, market or server config.
