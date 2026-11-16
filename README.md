# mod-playerbot-autonomy

A planned autonomous-world simulation module for the [AzerothCore WotLK playerbots fork](https://github.com/mod-playerbots/azerothcore-wotlk), designed to work alongside [RovxBot/mod-playerbots](https://github.com/RovxBot/mod-playerbots) and [RovxBot/mod-dungeon-clear](https://github.com/RovxBot/mod-dungeon-clear).

> **Status: foundation and M1 contract only.** The module registers a disabled-by-default, inert WorldScript and now includes a proposed character-enrollment schema, pure two-phase lifecycle decision rules and tests. **No database adapter, runtime enrollment, reset protection or recruitment is installed.**

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

The only current enabled-state effect is a startup log that says runtime features are not yet implemented; there is no active bot manipulation, even if the option is changed to `1`.

### Fast, standalone policy and enrollment tests

Without an AzerothCore checkout:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/PopulationPolicyTests.cpp -o /tmp/autonomy-policy-tests
/tmp/autonomy-policy-tests
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/EnrollmentStateTests.cpp -o /tmp/autonomy-enrollment-tests
/tmp/autonomy-enrollment-tests
python3 -m unittest discover -s tests -p 'test_*.py'
```

This tests **pure decisions and static migration structure only**. CI runs the C++ tests in C++17 and C++20 and checks pinned dependency source contracts. Nothing here executes the SQL migration, connects to MySQL, installs engine hooks or substitutes for a full server test.

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
