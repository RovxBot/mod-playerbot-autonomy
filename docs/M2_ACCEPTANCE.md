# M2 review acceptance report — 19 November 2026

This is source/test review readiness, with a **simulated assembly boundary**.
The operator's existing M1/founding/recruitment/restart acceptance remains the
baseline. No production database, guild identity, deployment setting or server
process was accessed or changed during this work.

## Review order and compatibility

The curated-names branch targets main. The roster/operations branch is stacked
on that branch so both components and their shared harness are tested together.
Review/merge naming first; the operations PR can then target main. Neither PR
is automatically merged. No companion playerbots patch is needed: every leased
character is already M1 PERSISTENT, and the existing provider owns maintenance
protection. New cache lease facts prevent enrollment/recruitment overlap.

| Dependency | Exact source |
| --- | --- |
| Supported core, test-staging | `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` |
| Playerbots, master | `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59` |
| Existing dungeon-clear source contract, no calls added | `b1be76d93da4b409daa7a2c9a8f27301e540af4a` |

No unrelated local core/module checkout was used for runtime API assumptions.
The checked dependency pair is a source/header compatibility pin, not proof of
combined worldserver compilation/linking or the exact live binary's provenance.

## Observed local results

| Check | Observed result |
| --- | --- |
| Ten standalone policy/controller/runtime-boundary suites in each standard | All passed in C++17 and C++20 |
| Python discovery without a DB | 57 tests, 49 explicitly skipped DB cases; passed |
| Python discovery with disposable MySQL 8.0.44 | 57 passed, no skips |
| Python discovery with disposable MySQL 8.4.10 | 57 passed, no skips |
| Production stores/runtime + actual pinned core binary decoder on MySQL 8.0.44 and 8.4.10 | Passed, including both new components and existing regressions |
| Real dependency-header syntax, GNU C++20 required by the pinned core headers | 15 module translation units passed |
| Production name explanation adapter + actual core charter/Unicode/UTF-8/language functions | Passed; 44 accepted and 2 rejected in the documented fixture |
| Pinned core/playerbots/dungeon-clear source contracts | Passed (source shape only) |
| Scaffold and touched new C++ formatting checks | Passed |

The binary tests cover revisioned operation identity above 2^53, all-or-nothing
lease conflicts, replay, lost/late acknowledgements, ACTIVE interruption,
disabled restart recovery, timeout/cancellation/repeated cleanup, backward
wall-clock correction, four simultaneous simulations and at most one scheduled
SQL job per tick. They exercise actual roster observation calls through fake
engine objects, actual prepared store bindings, real transactions and the
unmodified core binary decoder. Engine fakes are **not** live role or group
acceptance. Existing founding visibility/recovery and 8,250-character churn
regressions still pass. Four 20-member guild fixture identities/founding intents
and character level/money remain unchanged.

Name tests verify the exact original 46-candidate hash, Unicode preservation,
case collision rollback, immutable retry/restart intent names and exhausted
pools. `CyLx-亚服` rejects U+002D; `*The Un*named GDKP` rejects U+002A.
Runtime word filters and strict-name settings can reject additional candidates;
no production configuration or filter data was queried.

Reproduction commands and behaviour are documented in [M2 operations](M2_OPERATIONS.md)
and [guild names](GUILD_NAMES.md). The MySQL harness requires an explicit
`AUTONOMY_MYSQL_SOCKET`/host and a database named `autonomy_test[_suffix]`;
its fixtures are public minimal schemas, never production copies.

## Delivered and remaining

Delivered: bounded managed-guild capability cache; deterministic five-role
planner; explicit exclusions/diagnostics; default-off configuration with a
separate default read-only mode; revisioned generic operation journal; exclusive
local/durable reservations; finite timeout/cleanup/recovery; curated naming.

Remaining before M2 runtime acceptance: one later **batched** worldserver build,
link/startup, live read-only roster observations and simulated-operation pilot,
baseline map/AI/CPU/DB measurements and the issue #13 soak. Do not repeat the
already accepted M1/founding/restart pilot. Actual group creation remains
unavailable behind its separate default-off gate; ACTIVE in this implementation
is a simulation state. Stable live bot-led parties and target-specific dungeon
access checks remain future integration gates. M3 additionally needs a trusted
dungeon-clear executor. No dungeon run, PvP, raid or economy orchestration is
claimed. Issues #6, #7 and #13 stay open for their remaining acceptance.

Rollout is deferred to operator review and the later batch. Keep all M2 gates
at their distributed defaults. The module-only operation migration is needed
before installing this source, including disabled restart cleanup. Disable M2
or restore dry-run to cancel simulations, observe zero leases, and retain M1
protection/schema/history throughout rollback. Naming needs no migration.
