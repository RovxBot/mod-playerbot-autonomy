# Roadmap

This is the intended delivery sequence, **not a claim of completed functionality**.

## M0 — Foundation (current scaffold)
- AzerothCore registration, inert opt-in config and script loader.
- Detailed AGENTS.md, architecture, roadmap, PR template, lightweight CI.
- Engine-independent population classification policy and standalone unit tests.
- **Build evidence:** standalone policy CI green; 15 Nov 2026 homelab image build compiled/linked the module and published a worldserver image. The new image is now verified running Ready with zero restarts; direct hook INFO logging, broader behaviour regression and final licence decision remain outstanding (see `docs/INTEGRATION.md`).
- **Exit:** review integration evidence, validate a live startup on the new image, and resolve or explicitly track licensing obligations; do not claim all M0 exit gates complete from build success alone.

## M1 — Two populations and durable lifecycle (first runtime priority)
- Unguilded random bots retain existing reroll, level brackets, zone rotation, gathering, crafting and auctions.
- Guilded random bots stop destructive maintenance; actual level, gear, professions, money, skills and achievements survive.
- Recruited bots transition without artificial rewards; accidental guild loss does not reset a persistent character.
- Explicit retirement path; safe boundary for human-managed guilds and selfbots.
- Audit every existing reset/refresh, randomise, group-dissolve, teleport and population-management call path.
- **Exit:** verified on live server through several manager cycles and a restart, with both bot classes tested.

## M2 — Autonomous guilds and activity ownership

The [managed founding candidate](MANAGED_GUILDS.md) implements opt-in founding
and explicit ownership; the [recruitment candidate](RECRUITMENT.md) adds automated
STAGE/guild/CONFIRM integration. Their full-server acceptance is still pending.

- Safe recruiting from unguilded population into eligible bot-owned guilds.
- Durable guild rosters, recruitment caps, activity budgets and member availability.
- Central scheduler, exclusive activity leases, failure-safe group formation and operations history.
- No real-player recruitment or human-guild governance by default.
- **Exit:** existing bot guild grows without hijacking players, guilded members persist and leases survive/recover from crashes.

## M3 — Five-player dungeon expeditions
- Trusted, narrow opt-in integration contract with mod-dungeon-clear.
- Formation of existing tank/heal/3 DPS parties; no GM master, free gear or fake loot.
- Level/gear/access validation, dungeon allowlist, travel/entry, completion and watchdog recovery.
- **Exit:** real existing five-bot party completes one proven dungeon, retains true rewards, returns to normal tasks; RDF and GM tests remain functional.

## M4 — Living guild economy
- Coordinate existing profession work, bank donations, authorized withdrawals and vendor supplies.
- Prioritize useful loot, legitimate crafting inputs and surplus auctions without duplicating existing mechanics.
- **Exit:** repeatable gather -> craft -> equip/deposit/sell loop with auditable item and gold accounting.

## M5 — Organised open-world groups and PvP
- Reuse the generic operation roster/lease framework for escort, defence and conflict.
- First controlled Southshore/Tarren Mill combat scenario, opt-in and performance-capped.
- **Exit:** squads assemble, travel, fight, retreat and recover; no permanent guard farms or harassment of real players.

## M6 — Guild rivalries and town/city assaults
- Per-guild strategic objectives, faction-correct rivalry and multi-group coordination.
- Opt-in target allowlist, cooldowns, safety perimeters and anti-grief controls.
- **Exit:** controlled world events do not compromise normal player gameplay or server performance.

## M7 — Autonomous raids
- Ten-player raid composition and supporting mechanics first; then 25-player guild/cooperative raids.
- Use real saved characters, consumables, roles, instance access and loot.
- **Exit:** stable raid encounter progression, durable character gains, predictable teardown and measured capacity.

## Implementation policy

Deliver in vertical slices; no giant PRs covering multiple milestones. Every issue must specify objective, prerequisites, safety constraints, acceptance tests and evidence. Test and pin external module APIs before depending on them. Prefer reusing existing systems to implementing parallel ones.
