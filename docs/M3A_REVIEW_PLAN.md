# Classic review correction plan — 20 November 2026

Review bases: autonomy `e056193a07814872bf49d2f75fe9e650a73dd4a0`, provider
`fad8a7691197f5be922f00d6cd2d3430e68dc48c`, unchanged owner playerbots
`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`, read-only core test-staging
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`. Scope: issues #8, #9 and #30;
draft PRs #32 and owner dungeon-clear #11. No combined build or realm actions.

1. Share the provider's actual equipment predicate with core group admission
   and follower revalidation. Execute it with deterministic item fakes and
   assert missing/broken mandatory equipment prevents any group creation.
2. Investigate Actions using exact-head run/check queries and dispatch only
   `autonomy.yml`. Verify its four lightweight commands locally; run consumer
   source contracts and actual-header syntax against the pinned dependencies.
   Do not enable Actions or run any general build workflow.
3. Audit existing portal movement, normal core admission, binding teardown and
   leased-member login. Stop M3B implementation if safe recovery is unverified.
   Do not patch core/playerbots, relax M1 or replace portal admission.
4. Extend the actual scheduler/store/controller fixture for failed entry,
   separated members, assembly/entry wait timeout, cancellation and wrong
   instance observations. Prove retained exclusive leases and M1 protection
   until executor, core group and SQL cleanup are confirmed. These are failure
   containment tests, not an implementation of portal movement/recovery.
5. Run all standalone C++17/C++20 suites, Python tests and actual prepared SQL
   on disposable MySQL 8, source contracts, scaffold and pinned-header syntax.
   Record immutable heads and the separate combined-build/live gates.

All engine operations stay on the existing world update barrier after map
workers join. Equipment work is bounded by five members and equipment slots.
No SQL, file I/O, realm scans, movement or additional runtime activity is added.
