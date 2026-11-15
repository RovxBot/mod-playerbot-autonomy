# Integration verification and release gates

A successful build, a source-contract check and live worldserver startup are **different validation levels**. Never present one as proof of another.

## Recorded build evidence — 15 November 2026

- [Successful homelab build workflow](https://github.com/RovxBot/homelab-infra/actions/runs/43679235448)
- [Build job log](https://github.com/RovxBot/homelab-infra/actions/runs/43679235448/job/132706024364)
- Workflow source (homelab-infra): **b23d84c487f186e0d00b72efc781ff573904d395**
- Build outcome: **success**. The log explicitly clones the autonomy module from **main**, compiles both **AutonomyModule.cpp** and **mod_playerbot_autonomy_loader.cpp**, links the worldserver, and reports **[100%] Built target worldserver**.
- Published image: **ghcr.io/rovxbot/azerothcore-wotlk:worldserver-test-staging-b23d84c487f186e0d00b72efc781ff573904d395**; manifest digest **sha256:fbd9affc7fffb94ec29b3e8a1701f896a3e17e105e3f31de0a49cf0073c3bb67**.
- Build config (confirmed at that homelab-infra commit):
  - Core: **mod-playerbots/azerothcore-wotlk**, branch **test-staging**.
  - Playerbots: **RovxBot/mod-playerbots**, branch **master**.
  - Dungeon-clear: **RovxBot/mod-dungeon-clear**, branch **master**.
  - Autonomy: **RovxBot/mod-playerbot-autonomy**, branch **main**.
  - Evidence: homelab-infra **ops/wotlk-images/azerothcore.repo**, **ops/wotlk-images/azerothcore.ref**, **apps/wotlk/config/modules.txt**, and the WotLK image-build workflow.

**Important reproducibility limitation:** the image workflow prints branch names and module clone operations, but not the resolved SHA of each cloned dependency. The exact pinned snapshots in the inexpensive contract CI below are *not* necessarily identical to the commits compiled in this historical build. Future full builds should publish an exact source-revision manifest.

## Actual core integration contracts

Source-verified against the target core family:

- **src/cmake/ac_macros.cmake** defines **AC_ADD_SCRIPT**.
- **modules/CMakeLists.txt** generates module-loader function names and automatically discovers/installs **conf/*.conf.dist** using **CopyModuleConfig**.
- **AC_ADD_CONFIG_FILE** is a **deprecated no-op** in this core, so this module no longer invokes it.
- **WorldScript.h** declares **WORLDHOOK_ON_STARTUP** and **OnStartup**.
- **Config.h** supports **GetOption** with an explicit default.
- The build logs show the existing loader and script sources compiled into the modules target.

**These are source-contract observations**, not guarantees that arbitrary new engine integration code will compile.

## Live runtime status at time of review

A read-only Kubernetes check on 15 November 2026 found a Ready worldserver pod running the **older image**, tagged **worldserver-test-staging-7d1d58dccafee70e2f0ba57978bfbae3684b6edc**. It had zero container restarts. It was **not** using the new b23d84c image. Consequently the new autonomy module's startup log and runtime registration have **not yet been verified on the live server**.

Do not roll out the new image as part of documentation/CI maintenance.

## Lightweight PR validation (no full core build)

The **Standalone policy checks** workflow runs:

1. Standalone population-policy C++ tests under **C++17 and C++20**.
2. Python scaffold validation.
3. Python self-tests for the source-contract checker.
4. Sparse, read-only checkouts of a **pinned** core, playerbots and dungeon-clear snapshot, followed by **tools/check_integration_contracts.py**. The checker confirms the expected loader, CMake config discovery, startup hook, config API and current dependency entry points still have the required source shapes.

This job **does not** compile/link the game server, exercise runtime data, access a production database or deploy anything. Its dependency SHAs are pinned for deterministic runs; refresh them deliberately when updating the supported source combination. A green check does not prove compatibility with an unpinned upstream branch tip.

Run the standalone checks from the autonomy checkout:

~~~sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc \
    tests/PopulationPolicyTests.cpp -o /tmp/autonomy-policy-tests
/tmp/autonomy-policy-tests
c++ -std=c++20 -Wall -Wextra -Werror -pedantic -Isrc \
    tests/PopulationPolicyTests.cpp -o /tmp/autonomy-policy-tests
/tmp/autonomy-policy-tests
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tools/verify_scaffold.py
~~~

When compatible local dependency source trees are present:

~~~sh
python3 tools/check_integration_contracts.py \
    --core ../azerothcore-wotlk \
    --playerbots ../mod-playerbots \
    --dungeon-clear ../mod-dungeon-clear \
    --autonomy .
~~~

Paths above are an example, not a requirement to rearrange current checkouts.

## Release gate after a deliberate future deployment

1. Record the running worldserver image tag/digest and module source revision.
2. On first startup with the default **PlayerbotAutonomy.Enable = 0**, check for the expected **mod-playerbot-autonomy: disabled (foundation only)** log line.
3. Verify worldserver readiness and existing manual dungeon-clear, RDF, guilds, gathering, crafting and auctions remain functional.
4. Optionally on a controlled restart, set **PlayerbotAutonomy.Enable = 1**. At this stage it should only log **foundation enabled; no runtime features are implemented**. Restore disabled configuration afterward.
5. Add runtime evidence to **issue #3**. Only mark its startup acceptance gate complete after verifying the **new** image; do not infer runtime from build success.

No routine PR should trigger the prior multi-hour image build. New C++/runtime integration should instead request a separate consciously scoped full build and live test.
