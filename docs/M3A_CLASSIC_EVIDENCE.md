# Classic pilot verification — 20 November 2026

The [review correction evidence](M3A_CLASSIC_REVIEW_EVIDENCE.md) supersedes the
provider pin below for new builds and records shared equipment admission,
failure containment, the remote Actions blocker and the blocked M3B prerequisite.
The results below remain the historical pre-correction record.

Source candidate only. The exact compatible dependencies and live acceptance
runbook are in [M3A_CLASSIC_PILOT.md](M3A_CLASSIC_PILOT.md). The autonomy PR records
its immutable consumer head. Provider pin:
`fad8a7691197f5be922f00d6cd2d3430e68dc48c`; core header/decoder pin:
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`; unchanged playerbots pin:
`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`.

## Executed checks

| Check | Result and boundary |
| --- | --- |
| Autonomy standalone suites, C++17/C++20 | All passed: population, enrollment state/cache/transition/runtime (missing and present maintenance interface), founding, guild names, recruitment, roster/operations, real executor and actual CoreGroupPort with engine fakes. Compiled with `-Wall -Wextra -Werror -pedantic`. |
| Actual core-group adapter, missing and prior v1 profile headers, both standards | Passed: refused admission before any group mutation. Matched v2 headers pass the real adapter tests with fake core APIs. |
| Owner provider `python3 t/check_autonomy.py` | Passed both standards: production bridge controller, unchanged chat authorization body, actual encounter/spawn adapter body with fake data. Tests all partial four-boss masks, non-contiguous indices, wrong definition/map/difficulty/credit/spawn, unknown profile, UK/Stockade/heroic/Wrath rejection, levels/equipment, forged/expired/replayed/mismatched permits, wrong-version/missing providers, observation mismatch and cleanup. |
| `python -m unittest discover -s tests -p 'test_*.py' -v` | **58 tests, zero skips, OK** on each of isolated MySQL **8.0.44** and **8.4.10**. |
| `tools/check_prepared_mysql.py` with pinned DC | Passed on both versions. Actual stores, prepared bindings, unmodified core binary decoder, enrollment/guild/operation runtimes and both actual controllers; engine group/instance/run/exit evidence is injected. |
| Same binary harness without DC | Passed on 8.4.10: M1/guilds/simulations work; no real admission/group/leases without the profile provider. |
| Mixed v1/v2 ABI executable, C++17 | Passed: linked the actual old API header and current API in separate translation units; old owner/executor registration cannot populate the v2 provider slots. No cross-layout callback occurs. |
| `tools/check_runtime_syntax.py --mode autonomy` | **24 PASS**, GNU C++20 against actual pinned core/playerbots/DC headers. Includes actual CoreGroupPort, OperationRuntime, provider, shared DC initialization and existing recovery guards. This is syntax checking, no object/link/startup. |
| Source contracts, scaffold and actual core guild-name adapter checks | Passed. The latter retains the prior fixture's 44 accepted/two rejected names; no naming changes. |
| DC config-read, MSVC-portability and determinism checks | Passed. No raw config reads or new RNG in execution. |

The binary SQL harness printed, on both versions:

```text
PASS production partial assembly compensation: two persisted members, core SQL proof,
one disband, durable delete confirmation before five-lease release
PASS matched production autonomy/DC controllers + actual SQL: authentic permit,
single launch despite lost ACK, real-mode ACTIVE only after executor ACK, terminal evidence,
instance-safe retained leases, confirmed cleanup; engine run/exit evidence is injected
PASS production real scheduler/store: creation fence/receipt, lost acknowledgement,
no simulated ACTIVE, unsafe-expiry protection, disabled cleanup, half-group restart manual gate;
group APIs are faked
PASS character progression unchanged; no worldserver build/link/startup
```

The retained-ownership fixture scheduled 28 query jobs/eight write jobs over
267 scheduler ticks, injecting five failed acknowledgements, with one create,
four adds and one disband. Example maximum fake scheduler samples stayed below
1 ms. These tiny fixtures do not measure production SQL load, map latency,
navigation, combat reactions, realm capacity or a soak. Four-guild identities
and character progression comparisons are **disposable fixtures**, not a live
realm inspection.

## Reproduction and review limits

The standalone commands are in `.github/workflows/policy.yml`; the new adapter
command adds `-I<provider>/src` after `-Itests/group_fakes`. Run
`python3 t/check_autonomy.py` from the provider checkout. The MySQL fixture
requires explicit `AUTONOMY_MYSQL_SOCKET` (or host) and an
`AUTONOMY_MYSQL_DATABASE=autonomy_test[_suffix]` throwaway database, and rejects
other names. Both local servers used fresh `/tmp` data directories, private
Unix sockets and `--skip-networking`. Only public minimal fixture tables were
created; no production DB was accessed.

```sh
python3 tools/check_prepared_mysql.py --core <pinned-core> \
    --playerbots <pinned-playerbots> --dungeon-clear <pinned-provider>
python3 tools/check_runtime_syntax.py --core <pinned-core> \
    --playerbots <pinned-playerbots> --dungeon-clear <pinned-provider> --mode autonomy
python3 tools/check_integration_contracts.py --core <pinned-core> \
    --playerbots <pinned-playerbots> --dungeon-clear <pinned-provider> --autonomy .
python3 tools/verify_scaffold.py
```

Local include/library flags pointed at the existing isolated Boost/MySQL client
development files; the SQL decoder needs C++20, while module policies/controllers
are tested in C++17 as required. No dependency source patches were applied.

Combined compilation/linking/startup, actual loaded DBC/mmap validation, lawful
portal entry/exit, masterless reaction timing, real four-encounter kills,
earned-reward retention, safe real cleanup, player/GM/RDF/BG regression and realm
load acceptance remain unexecuted. No combined image build was requested,
dispatched or run. No deployment, Kubernetes, live config, production SQL or
guild/character mutation occurred. No issue is closed on this source evidence.
