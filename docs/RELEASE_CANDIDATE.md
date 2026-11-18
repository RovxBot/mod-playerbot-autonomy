# One manually initiated WotLK release-candidate build

Prepared 18 November 2026. No full worldserver build, deployment, production DB
mutation or merge is authorized or performed by this work. Source/SQL/controller
checks prepare a candidate; they do not establish game-server acceptance.

## Source set and review order

| Component | Candidate source |
| --- | --- |
| Core, read-only | mod-playerbots/azerothcore-wotlk test-staging `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` |
| Playerbots baseline | RovxBot/mod-playerbots master `08b6bd127513d7273b1fcb765e537c5a3cd813ec` |
| Playerbots companion | RovxBot/mod-playerbots `rc/enrollment-handoff`, `422db5a50b429db01ab481ba597b928c87b0eb98`, own-fork draft PR |
| Dungeon-clear, unchanged | RovxBot/mod-dungeon-clear `1a336ad58debdc2ffee598e78ce216ccff35153f` |
| Autonomy | RovxBot/mod-playerbot-autonomy `rc/guild-recruitment`; exact final SHA is captured by the manifest below and its PR head |

Autonomy's focused review stack is read-only diagnostics (#20), M1 safety (#21),
managed founding (#22), then recruitment. The top recruitment commit contains all
three implementation slices. Pin the **resolved commits**, not moving branch
names. Each PR's metadata identifies its exact head and parent. The owner
explicitly requires only their own repositories/forks and playerbots master as
target. Existing playerbots source-branch governance may still reject that target;
resolve it through maintainer review, not by changing protections or bypassing CI.
No PR is merged by this task.

Capture exact sources from clean checkouts immediately before the manual build:

```sh
python3 tools/release_manifest.py --core /path/to/pinned-core \
  --playerbots /path/to/pinned-playerbots \
  --dungeon-clear /path/to/unchanged-dungeon-clear --autonomy . > release-manifest.json
```

The script is read-only and fails for dirty sources. Include this manifest,
compiler/dependency versions, config switches, SQL migration history and the
resulting image digest in build evidence. The supported core's headers require
GNU C++20; autonomy's own policies/controllers use C++17 constructs and are tested
under both C++17 and C++20. The historical deployed core/toolchain is not inferred
from the stale sibling checkout or from README examples. Test-staging core plus
this playerbots master source set passes syntax checks, but combined linking is
still unproven. No core code is changed.

## Cheap evidence already available

- Population, lifecycle, receipt, cache and actual M1 runtime/command tests under
  C++17/C++20, including missing provider, activity disable, Pending/Persistent
  restart, in-flight commit, active client accounts and fail-closed DB states.
- Founding controller tests under both standards: eight new guilds with 4/4
  faction balance, dry run, class/faction/human/group exclusion, unique jobs,
  exact stage proof, interrupted create quarantine and Pending confirmation.
- Recruitment controller tests under both standards: cap 70 including founder,
  one active job, bounded candidate selection, duplicate requests, leader/officer
  authorization, human/unmanaged guild exclusion, capacity/authority changes,
  Pending retention, disable/restart and no repeated add after restart.
- Production prepared enrollment/guild/recruitment SQL on disposable MySQL
  8.4.10 fixtures: migrations executed twice, enforced constraints, exact actor/
  request/revision proof, ownership identity, CAS/duplicate/audit rollback, restart
  windows and unchanged fixture level/money. CI additionally runs MySQL 8.0 and
  8.4.10; inspect the PR checks instead of assuming their outcome.
- Pinned real-core GNU C++20 **syntax only** for module runtime and maintenance
  sources, plus source contracts, scaffold and formatting. No objects, link,
  worldserver startup or gameplay execution.

SQL fixtures are minimal public-schema boundaries, not a copy of the live realm.
The controller ports simulate engine outcomes; they do not execute real Guild or
WorldSession methods. Header/source checks do not prove callback/link order or
thread timing. M1 is deployed according to the operator; this release candidate
has not been deployed. Trela's historical STAGE rejection remains unverified.

## Readiness checklist for the owner

- [ ] Review all focused autonomy PRs and the playerbots companion. Keep full-build
  workflows gated until deliberately starting the single manual build. Review the
  playerbots master-target source-policy failure if present. Do not auto-merge.
- [ ] Pin the four sources above and capture a clean revision manifest. Review
  startup include/loader registration and compatible actual toolchain. Confirm
  the intended build uses these exact heads rather than current master/main tips.
- [ ] Back up character/enrollment/audit state. On a disposable copy of the actual
  characters schema, apply in order: `2026_11_15_00`, `2026_11_15_01`,
  `2026_11_18_00`, `2026_11_18_01` from `data/sql/db-characters/`. Verify the actual
  updater/import path; the historical homelab updater was disabled. No routine
  rollback drops tables or erases receipts.
- [ ] Manually initiate one complete combined worldserver compile/link, retaining
  logs, source manifest and image digest. Verify both module loader entry points
  and static shared provider state. No automatic build/deployment is included.
- [ ] In a separately authorized isolated pilot, start with guild/recruitment off
  and required M1 protection installed. Confirm provider registration precedes bot
  factories, unknowns stay protected until reconciliation, and known unowned
  filler retains normal maintenance. Verify shutdown after maps unload.
- [ ] Enable guild feature with dry-run=1. Audit existing guilds and ownership;
  Git Commit, Bangerzoids and every guild lacking explicit ownership remain
  unmanaged. Run `autonomy diagnose 187`; never infer Trela's eligibility from a
  prefix, broad factory list or unverified historical facts.
- [ ] Begin with two managed guilds (one per faction), cap 3, low budgets and real
  before/after progression snapshots. Set founding dry-run=0, then opt into
  recruitment separately. Keep AllowEnrollmentCommands=0. Verify real guild
  create/add, STAGE/CONFIRM receipts, rank rights, capacity and no item/XP/money/
  talent/profession/quest/achievement alteration beyond normal gameplay.
- [ ] Interrupt after intent, STAGE, CREATING, AddMember and before CONFIRM. Test
  config reload, activity disable, restart, DB outage/timeout, stale cache, human
  master/account login, other guild/group and leader/member changes. Confirm
  no duplicate guild/member, automatic adoption, retirement or protection gap.
  Ambiguous CREATING and conflicting jobs must quarantine without deleting guilds.
- [ ] Increase only after canary success to eight managed guilds, 50% Alliance,
  cap 70. Observe pmon/world/map ticks, memory, query load, throughput, failures,
  scheduler duration and retained Pending jobs. Verify ordinary filler, natural
  progression, professions/auctions, guild bank, RDF and existing dungeon-clear
  behavior. No new dungeon or economy execution is included.
- [ ] Prove rollback by stopping new activities while protection remains installed;
  preserve all intent and lifecycle tables. If removing/downgrading protection,
  keep worldserver stopped until a compatible binary is restored. Do not call
  mass retirement, guild deletion or table drops a rollback.

## Outstanding blockers and limits

Full build/link, actual startup/provider ordering, engine async save/login/guild
persistence timing, real progression/restart canary and realm load remain manual
gates. Current live account/session classification for Trela is unknown. Playerbots Actions is currently disabled: the standalone dispatch was rejected
with HTTP 422 and its PR has no check runs. Local checks are recorded separately;
no Actions setting was changed. Its promotion-policy check also needs maintainer
disposition for master-target PRs if Actions is re-enabled. License/distribution questions tracked in #3 remain unresolved; no large
external implementation or license choice is imported here. Issues #5, #6, #13
and #17 stay open until their actual acceptance gates are met. A successful cheap
test run is not grounds for closing them or enabling the realm automatically.
