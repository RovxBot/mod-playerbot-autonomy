# Classic review correction verification — 20 November 2026

Updated draft PR #32 and owner dungeon-clear PR #11. No M3B movement PR:
the [RFC prerequisite audit and later acceptance plan](M3B_RFC_PREREQUISITE.md)
record the required stop on unverified leased-member login and binding cleanup.

## Exact source set

| Component | Revision |
| --- | --- |
| Consumer review base | `e056193a07814872bf49d2f75fe9e650a73dd4a0`; use the immutable corrected head on PR #32 |
| Corrected provider | `a6627e6ad05445737090b41618346088e8637e1d` |
| Unchanged owner playerbots | `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59` |
| Read-only core header/decoder snapshot | `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6` |

Core/playerbots worktrees were clean at these pins. The consumer workflow and
prepared harness now pin the corrected provider. These validation SHAs are not
proof of the live realm's resolved source revisions or branch compatibility.

## Executed checks

* All existing autonomy standalone suites passed under C++17 and C++20 with
  `-Wall -Wextra -Werror -pedantic`: population, enrollment state/cache/
  transition/runtime (present and missing maintenance provider), founding,
  naming, recruitment, operations, real executor and actual CoreGroupPort.
* The actual CoreGroupPort fixture rejects missing main hand and each broken
  equipped slot for **each of five participants**, even with a high gear score,
  before any Create/Add call. Follower additions recheck changed equipment.
  It checks separated maps, different instance IDs and bindings after exit.
* Missing-profile actual CoreGroupPort variants passed under both standards:
  no real group mutation. No optional-provider behavior was relaxed.
* Provider `python3 t/check_autonomy.py`, config-read, MSVC-portability and
  determinism checks passed. Production controller, unchanged chat authorization,
  actual encounter/spawn adapter and shared equipment predicate run with fakes
  under C++17/C++20. The equipment helper accepts empty nonmandatory slots and
  rejects missing/broken main hand and every broken equipped item.
* **58 Python tests, zero skips, OK on each of disposable MySQL 8.0.44 and
  8.4.10.** Fresh `/tmp` data directories, private Unix sockets,
  `--skip-networking`, `autonomy_test_review`, public minimal fixtures only.
  Both local disposable servers were shut down after verification.
* Production prepared stores/runtimes, both actual controllers and the pinned
  unmodified core binary decoder passed on both MySQL versions. New cases:
  failed/no entry and separated-member wait timeout, kill-switch cancellation,
  wrong-instance executor observation, five exclusive leases retained beyond
  expiry, enrollment login allowance unable to override a lease, M1 protection
  retained after cleanup, and SQL-confirmed group absence before lease release.
  The MySQL 8.4.10 variant without DC also passed.
* **24 GNU C++20 actual-header syntax checks passed**, including the changed
  provider and CoreGroupPort. This uses `-fsyntax-only`: no object/link/startup.
* Current-consumer source contracts, scaffold, actual core guild-name check and
  whitespace checks passed. The corrected provider workflow's frozen consumer
  checker was separately extracted from exact commit `e056193...` and passed
  against the exact core/playerbots pins and corrected provider source.

The new binary fixture emitted on both versions:

```text
PASS entry failure containment with actual SQL/controllers: failed portal/separated wait
timeout, cancellation, instance mismatch, exclusive leases past expiry, M1 retained,
SQL-confirmed cleanup; entry/recovery/exit evidence is injected, no movement executed
```

Reproduce standalone commands from `.github/workflows/policy.yml` and provider
commands from its `autonomy.yml`. Reproduce the prepared harness and syntax
checks using the commands in [the original evidence](M3A_CLASSIC_EVIDENCE.md)
with the corrected provider pin above. A disposable MySQL connection/name must
be explicitly supplied; no production database was contacted.

## Remote Actions boundary

Original provider head `fad8a769...` and corrected head `a6627e6...` both returned
zero runs and zero check runs through all-event GitHub API queries. The workflow
is active; its `pull_request` trigger has no path/base filter, and prior PR/master
runs succeeded. Commits have no skip-CI message. A dispatch of only `autonomy.yml`
on the PR branch was rejected at both heads with HTTP 422:
`Actions has been disabled for this repository.` The permissions endpoint
contradictorily returned `enabled: true`. Administrative cause is unverified.
No settings were changed and no general build workflow was dispatched.

The corrected workflow explicitly checks out/prints the actual PR source SHA,
runs its four lightweight checks and checks pinned dependency source shapes.
Local results above are passing; **there is no green remote provider run**.

## Acceptance limits

Engine group/portal/instance/recovery/exit evidence in these tests is injected.
No portal action, real admission, automated recovery, combat, real reward or
live four-guild/progression observation occurred. No worldserver combined
compilation/link/startup, image build, merge, deployment, Kubernetes/live-config
change or production DB access occurred. Disposable guild/progression checks
do not assert live preservation. Integration smoke and at-level combat remain
distinct, unexecuted gates. Expansion bounds are operator inputs, not progression
discovery. Issues #8, #9 and #30 remain open.
