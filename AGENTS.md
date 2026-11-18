# AGENTS.md — mod-playerbot-autonomy

## Mission and status

This repository will become the **autonomous-world director** for AzerothCore 3.3.5a random playerbots. It is a coordinating module, **not** another bot AI, pathfinder, profession module or dungeon-clear implementation.

Current source status is **M1 runtime plus a default-off guild founding/recruitment release candidate**. The operator reports M1 deployed. The new guild code has deterministic controller, disposable SQL and pinned-header evidence, but no combined server build or live acceptance yet. Party forming, dungeon execution, raids, PvP and guild economy are not implemented. Do not describe intended behaviour or source tests as production acceptance.

Read `README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/INTEGRATION.md`, `docs/LICENSING.md`, `docs/ENROLLMENT.md` and the relevant GitHub issue before changing code. Keep the design aligned with the issue's acceptance criteria.

### Source-of-truth repositories

- This module: https://github.com/RovxBot/mod-playerbot-autonomy
- Playerbots fork: https://github.com/RovxBot/mod-playerbots
- Dungeon-clear fork: https://github.com/RovxBot/mod-dungeon-clear
- Supported core family: https://github.com/mod-playerbots/azerothcore-wotlk (playerbots core fork, **not stock AzerothCore**)

**Inspect the actual checked-out commits and their AGENTS.md files before making integration assumptions.** A user's intended core branch may be `test-staging`; never silently mix it with incompatible module branches. Do not assume source paths, APIs, existing integrations or tests based only on README examples.

## Architectural rules

1. **Keep orchestration here.** Population classification, guild objectives, activity priorities, reservations, rosters, expedition state, economy planning, PvP planning and observability belong in this repo.
2. **Reuse existing execution systems.** mod-playerbots owns random-bot login, level balancing, movement, combat, gathering, crafting, auctioning, inventories and its profession loop. mod-dungeon-clear owns dungeon routing and encounter execution. No duplicate versions of those systems.
3. **Minimise cross-module patches.** Propose narrow, opt-in capability APIs in dependent forks only when necessary. Include exact compatibility and rollout notes; track the dependent changes in separate linked PRs. Never patch core or another module opportunistically.
4. **Never spoof human or GM control.** Autonomous dungeon start requires an explicitly authorised internal integration. Keep regular player and GM chat command checks intact.
5. **Plan generic operations.** Five-player dungeons, 10-/25-player raids and open-world PvP should ultimately use one roster/reservation/operation lifecycle; execution-specific controllers remain separate.
6. **Make shutdown and failure boring.** Every reservation, group, instance and active job must have a bounded timeout, a cleanup path and a restart recovery strategy before being enabled by default.
7. **Be evidence-first.** Cite actual source, tests, logs and reproduced behaviour in PRs. Report unverified assumptions as such.

## Non-negotiable population semantics

- **Unguilded random bots are WORLD FILLER.** They keep existing mod-playerbots level brackets, rotation, rerolls, exploration, gathering, crafting, auctions and normal world activity.
- **Guilded random bots are PERSISTENT.** Once recruited, their real level, gear, talents, skills, professions, money, quest progress and achievements must not be overwritten by maintenance rerolls or level resets. Recruitment is *not* a reward-granting event.
- **Persistent enrollment is durable.** A guild transfer, transient missing guild record or accidental disband must not silently send a character back to the random reset pool. Returning to filler requires an explicit, audited retirement transition.
- **Real players and selfbots are protected.** Exclude human-controlled characters from autonomy. Do not recruit into, command, withdraw from or otherwise change a human-managed guild without explicit future owner-approved design.
- **Guild membership and autonomous guild ownership are different questions.** Random bots in human-managed guilds may require persistent-character protection, but their guild is not an autonomous organisation. Do not infer bot-guild ownership from the guild leader account alone.
- **No fabricated economy.** Do not mint currency, duplicate loot, generate reward equipment or bypass normal access/instance/loot/auction restrictions to make a scenario work.
- **No change to unowned bots with master switch off.** A disabled feature must have no unintended side effects on any playerbot lifecycle, script, guild, group, item or auction.

The policy in `src/Policy/PopulationPolicy.h` is an **engine-independent policy seed**, not a runtime integration. It becomes authoritative only when all related maintenance and character update paths have been audited and safely hooked.

## Runtime, performance and thread safety

- Assume thousands of random bots. No O(N) realm scans per bot tick and no full guild/auction/inventory scans within hot AI triggers.
- Prefer one world-thread scheduler, cached eligible rosters, incremental work queues, strict per-tick work budgets and bounded concurrency.
- Never block map threads with synchronous SQL or file I/O. Treat map objects, sessions, groups, GUIDs, teleports and bot AI as world/map-thread-affine according to the actual core implementation.
- Persist **intent and lifecycle**, not raw pointers or volatile engine objects. After restart, revalidate character GUIDs, guild ownership, session state, party membership and dungeon binding before resuming or cleaning up.
- An activity lease must exclude the member from conflicting random teleports, destructive refresh/randomise/level resets, group dissolution, unrelated activities and population trims.
- Design explicit safety behaviour for disabled feature, lost group, logout, wipe, instance reset, stale lease, dead/stuck bots, server restart, config reload and module mismatch.
- Observe CPU time, memory, DB load, map tick time, active operation count, skip reasons and timeouts. Do not log every AI tick.
- PvP must remain opt-in with geographical limits, cooldowns, server-owner caps and guardrails against continual attacks on real players or starter areas.

## Repository and code conventions

- `CMakeLists.txt` registers AzerothCore scripts from `src/` and config from `conf/`.
- The generated AzerothCore loader expects **`Addmod_playerbot_autonomyScripts()`** for directory `mod-playerbot-autonomy`. Do not rename casually.
- The deployed AzerothCore build has C++17 compatibility constraints. Production module code must compile under the actual core/toolchain; do not assume C++20-only syntax is safe. Run standalone policy tests in **both C++17 and C++20**. Use four spaces, UTF-8/LF, trailing newline, scoped types and simple ownership. Prefer pure, testable decision functions separated from game-engine adapters.
- `src/Policy/` holds engine-independent decisions; `src/` top-level holds script entry points. Introduce `src/Director/`, `src/Guild/`, `src/Operations/`, `src/Economy/`, `src/Integration/` and `src/Persistence/` **only when implemented**; do not create dead abstractions.
- Document actual runtime configuration in `conf/mod_playerbot_autonomy.conf.dist`. New dangerous functionality defaults OFF.
- Prefer schema migrations over editing vendor/base SQL. On the supported core, module character DB SQL is discovered directly under `data/sql/db-characters/`; use the reviewed M1 migration and document any additional schema work. Never put code-generated secrets, private world databases or production logs in git.
- The licenses of the dependent forks differ; verify applicable licenses and attribution before copying code or selecting this repository's distribution license. Do not import large implementation files.
- Keep a single focused PR per completed, testable vertical slice; small compatibility patches should be separately linked and tested against pinned revisions.
- Do not merge PRs or change branch protections without an explicit request.

## Workflow for every issue

1. Read the issue, these instructions, relevant checked-out source, and related integration notes.
2. Identify real data and call paths, thread ownership, failure paths and performance budget before coding.
3. Write a short implementation and verification plan. For cross-repo API changes, pin exact commit refs and specify compatibility.
4. Add/update deterministic unit tests for pure policies and integration tests for engine boundaries where practical.
5. Run the fast standalone test for policy changes:
   `c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/PopulationPolicyTests.cpp -o /tmp/autonomy-policy-tests && /tmp/autonomy-policy-tests`
   Then repeat with `-std=c++20`, compile/run `tests/EnrollmentStateTests.cpp` under both C++17 and C++20, and run `python3 -m unittest discover -s tests -p 'test_*.py'` and `python3 tools/verify_scaffold.py`.
6. If the task requires a core build or live realm execution, do it only when the actual supported core and dependencies are available; report precisely what was built and tested. The PR source-contract workflow checks pinned external APIs, **not** compilation, linking or startup. Never substitute standalone/contract tests for game-server verification.
7. Self-review backward compatibility, dormant/disabled-state behaviour, pointer/thread safety, idempotency, rollback, real-player boundaries and load impact.
8. Open a PR with: problem, behaviour change, dependency commits, verification performed/results, rollout/rollback, and risks. Do not claim a successful test without output.
9. update open or close issues based on completed work, and link to PRs. Do not close issues without a PR or a clear explanation of why the issue is no longer relevant.
10. Never tag or mention a a PR or issue outside of this repository.

## Scope and milestones

- **M0 foundation:** build registration, config, policy seed, documentation, basic CI.
- **M1 population:** reliable guilded-persistent vs unguilded-filler lifecycle and cross-module reset guards.
- **M2 guilds:** safe recruitment, rosters, operations and leader decisions.
- **M3 expeditions:** real five-bot dungeon parties through a trusted dungeon-clear interface.
- **M4 economy:** guild supply, trading and loot handling through existing systems.
- **M5–M7 future:** coordinated world PvP, guild warfare, then 10-/25-player raids after proven performance.

A task is NOT done merely because a strategy is registered, a bot enters an instance or a test harness run finishes. See `docs/ROADMAP.md` for user-visible definitions of done.
