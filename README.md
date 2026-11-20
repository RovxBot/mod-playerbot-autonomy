# mod-playerbot-autonomy

A planned autonomous-world simulation module for the [AzerothCore WotLK playerbots fork](https://github.com/mod-playerbots/azerothcore-wotlk), designed to work alongside [RovxBot/mod-playerbots](https://github.com/RovxBot/mod-playerbots) and [RovxBot/mod-dungeon-clear](https://github.com/RovxBot/mod-dungeon-clear).

> **Status:** the operator reports M1 persistence, four managed guilds, founding/recruitment, interrupted-founding recovery and restart persistence accepted live. New changes in this branch remain review candidates; their standalone/SQL/header checks do not establish a combined worldserver build or live acceptance.

[Founding race fixes and explicit recovery](docs/GUILD_FOUNDING_RECOVERY.md),
[managed guild founding](docs/MANAGED_GUILDS.md), [automatic recruitment](docs/RECRUITMENT.md)
and the [manual release checklist](docs/RELEASE_CANDIDATE.md) document the
default-off director and its separate release gates.

[Curated guild naming](docs/GUILD_NAMES.md) documents the exact 46-name pool,
core validation/rejections and immutable names for existing founding intents.

[M2 roster planning and simulated operations](docs/M2_OPERATIONS.md) describes the
new default-off planner/coordinator and its separate build/live acceptance gates.
The operator reports the existing four managed guilds, M1 persistence and
founding/recruitment accepted live; this change does not repeat those pilots.

[M3A source candidate and acceptance report](docs/M3A_RELEASE.md) adds a separate
default-off normal-group executor and optional trusted dungeon-clear bridge.
Real operations require core roster/SQL confirmation and executor evidence;
simulations remain selectable. Travel, entry/exit and automatic binding/restart
recovery remain follow-up work. No combined server build or real dungeon pilot
has been performed for this candidate.

[Classic RFC profile and operator runbook](docs/M3A_CLASSIC_PILOT.md) supersedes
the original Utgarde Keep target. The default-off v2 bridge allows only normal
Ragefire Chasm, validates four actual encounter definitions and enforces a
Classic/TBC progression ceiling. One existing managed guild must be selected;
no character or guild progression is provisioned.

The [Classic review corrections and M3B prerequisite](docs/M3B_RFC_PREREQUISITE.md)
record the shared live equipment checks and the verified portal/recovery audit.
M3B entry/exit remains blocked on leased-member login and normal-bind cleanup;
no movement implementation or combined build/live acceptance is claimed.
Expansion ceilings are operator inputs and do not detect realm progression.

## Vision

Create a populated MMO world with two complementary types of random playerbots:

| | Unguilded world filler | Guilded persistent bot |
| --- | --- | --- |
| Role | Keep every zone and level bracket alive | Develop a lasting character and guild history |
| Randomisation and level resets | Existing mod-playerbots behaviour continues | No destructive rerolls or level resets |
| Gathering, crafting, auction house | Existing systems continue | Existing systems continue; future guild supply chain |
| Recruitment | Eligible to join bot guilds | Already recruited |
| Organised operations | Not initially | Future dungeons, world PvP and raids |

**Recruitment is the transition.** A random bot joining a guild retains its current character and becomes persistent; it receives no free levels, items, gold or skills. Persistent enrollment survives incidental guild loss until explicitly retired. Random bots in human-managed guilds retain their characters but are *not* automatically directed by this module.

Long-term goals include autonomous five-player dungeon runs, guild economies, Southshore/Tarren Mill battles, guild rivalries, town and city assaults, and 10-/25-player raids. Those are roadmap targets, **not implemented features**.

## Responsibility boundaries

- **mod-playerbots:** bot population, existing AI and combat strategies, characters, gathering, crafting, auctions, inventory, group management and random-bot maintenance.
- **mod-dungeon-clear:** dungeon navigation, encounters, party execution and dungeon progression.
- **mod-playerbot-autonomy:** population lifecycle policy, persistence decisions, guild planning, activity reservations, expedition orchestration and future economy/PvP/raid coordination.

This module must not duplicate the profession loop or the dungeon-clearing AI. Small opt-in extension interfaces may be needed in the other modules to block destructive random-bot maintenance and safely authorize masterless dungeon runs. Do not weaken the existing player/GM command permissions.

## Installation / current build

Check out the repository at `modules/mod-playerbot-autonomy/` alongside `modules/mod-playerbots/` in a matching, supported **playerbots fork of AzerothCore**, not stock AzerothCore. Consult the source module and core branch compatibility before building; do not assume `master`, `test-staging` and `Playerbot` are interchangeable.

`CMakeLists.txt` registers scripts under `src/`. The playerbots core automatically discovers and installs `conf/*.conf.dist`; its older `AC_ADD_CONFIG_FILE` macro is deprecated and has no effect. Set:

```ini
PlayerbotAutonomy.Enable = 0
```

Installing this safety revision requires the two enrollment migrations even with activities off and the matching playerbots v1 maintenance capability. If that capability is missing, worldserver deliberately aborts rather than allowing unprotected random-bot maintenance. Protection remains active with `Enable=0`, including after restart. With it, the provider blocks unknown candidates until the asynchronous cache has reconciled; explicit operator-enabled console/administrator-RA operations can then write enrollment and audit records, but only when PlayerbotAutonomy.AllowEnrollmentCommands=1. No guild or progression data is changed by enrollment. Full build and controlled realm validation remain required before rollout.

### Fast, standalone policy and enrollment tests

Without an AzerothCore checkout:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/PopulationPolicyTests.cpp -o /tmp/autonomy-policy-tests
/tmp/autonomy-policy-tests
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/EnrollmentStateTests.cpp -o /tmp/autonomy-enrollment-tests
/tmp/autonomy-enrollment-tests
python3 -m unittest discover -s tests -p 'test_*.py'
```

CI tests pure C++17/C++20 decisions, fake engine/DB boundaries, pinned maintenance-provider integration and the actual prepared SQL on disposable MySQL 8.x. None of these proves combined worldserver linkage, live startup, or an in-game persistence pilot.

[Prepared SQL regression evidence](docs/PREPARED_SQL_REGRESSIONS.md) explains the
enrollment revision decoder and guild binding fixes, and how to run the binary
prepared-statement harness with the production stores and pinned core decoder.

## Build validation and follow-up

The [15 November 2026 homelab WotLK build](https://github.com/RovxBot/homelab-infra/actions/runs/43679235448) compiled both autonomy source files and successfully linked/published the worldserver image. The build is verified, and a subsequent read-only Kubernetes check confirmed the **new image running Ready with zero restarts**. The module's INFO-level startup message is filtered by the production logging configuration, so execution of that specific hook has not been directly observed.

See [integration evidence, exact limitations and lightweight PR gates](docs/INTEGRATION.md). The [licence notice audit](docs/LICENSING.md) remains unresolved pending confirmation; do not import external code assuming licensing compatibility.

## Documentation and tracking

- [Agent instructions](AGENTS.md) — required behaviour for Codex and other coding agents.
- [Architecture and invariants](docs/ARCHITECTURE.md) — boundaries, lifecycle and safety constraints.
- [Delivery roadmap](docs/ROADMAP.md) — incrementally validated milestones.
- [Integration verification](docs/INTEGRATION.md) — proven image build, source contract checks, deployment gate.
- [Licensing review](docs/LICENSING.md) — observed notices and unresolved licence questions.
- [Enrollment lifecycle and schema](docs/ENROLLMENT.md) — pending-stage crash barrier, audit and integration plan; not deployed.
- [Issues](https://github.com/RovxBot/mod-playerbot-autonomy/issues) — implementation work and acceptance criteria.

All future functionality is opt-in, measurable and recoverable by default.
