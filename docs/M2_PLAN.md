# M2 implementation and verification plan

Baseline: autonomy `49ea3f3` (merged founding recovery), core test-staging
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`, playerbots master
`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`. The operator reports four
managed guilds (23–26), persistent recruitment and restart acceptance.
Those records, founding intents and deployment settings are outside this change.

1. Cache bounded value snapshots from explicit managed guilds on the world
   thread, after map updates have joined. Reuse playerbots spec/role/item-level
   APIs. Ambiguous specs/roles and offline equipment stay unknown. Check M1,
   account/control, faction, groups, queues, travel and instance activity.
2. Pure deterministic five-member planner, same-guild preference, optional
   cross-guild cooperation, explicit rejection counts and stale evidence checks.
3. Generic operation journal with immutable participant values and revisioned
   state changes. Atomic unique character leases, bounded expiry, async store,
   readback after every uncertain acknowledgement. Recovery cancels interrupted
   simulated operations; never resumes an executor. M1 remains authoritative.
4. Default-off scheduler and read-only console diagnostics. Simulated assembly
   only: no group/teleport/instance/dungeon-clear/reward calls. Actual assembly
   remains gated until a separately reviewed executor and live validation exist.
5. Independent curated naming policy using core charter validation, core guild
   collisions and pending intents; keep selected names immutable on retries.
6. Run C++17/C++20 policies/controllers, existing Python suites, production SQL
   and binary-store tests on disposable MySQL, and real pinned-header syntax.
   No core/container build, production SQL, deployment, restart or merge.

Review PRs link #6, #7 and #13 without closing outstanding live acceptance.
Report CI URL when pushed; do not wait for CI.
