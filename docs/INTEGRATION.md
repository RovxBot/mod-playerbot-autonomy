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

## Live deployment verification — 15 November 2026

A **second**, read-only Kubernetes inspection after the operator's deployment confirmed:

- Namespace: **wotlk**.
- Worldserver pod: **wotlk-worldserver-8474749446-2sth2**.
- Running image tag: **worldserver-test-staging-b23d84c487f186e0d00b72efc781ff573904d395**, matching the successful image build above.
- State: **Running**, **Ready**, **zero container restarts**.
- Authserver pod: **wotlk-authserver-d986db4cf-69frm**, likewise using the **b23d84c...** authserver image; **Running**, **Ready**, **zero restarts**.
- The live worldserver started successfully with the **new image**. No deployment or configuration changes were made during this review.

**Important remaining verification distinction:** the live worldserver container logs did **not** show the expected `mod-playerbot-autonomy: disabled (foundation only)` message. This is expected given the currently checked-in production logging settings in
`RovxBot/homelab-infra/apps/wotlk/config/worldserver.conf`:
`Logger.module=2,Console Errors` and `Appender.Console=1,2,...`. The core's
`LogCommon.h` defines level **2 = ERROR**, **4 = INFO**, while the autonomy startup path uses `LOG_INFO("module", ...)`. Thus the configured logger and console appender filter the message out.
**Absence of this INFO log is not evidence the startup callback failed, but it is also not direct confirmation the callback executed.**

This establishes successful startup of the linked worldserver image, not a detailed gameplay regression test or explicit observation of the autonomy startup hook. If direct hook evidence is later required, use a **controlled temporary logging change with both logger and appender levels set to INFO** or a purpose-built future module status check; do not broaden production logging just for this documentation task. The default module switch remains `0` in the distributed config.

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

## Remaining runtime and release gates

1. **Completed:** new worldserver image tag verified on the live pod; container is Ready and has zero restarts.
2. **Remaining:** record a startup-hook observation if needed using suitably configured module and console/file logger levels; don't treat a suppressed INFO message as missing registration.
3. **Remaining:** explicitly verify existing manual dungeon-clear, RDF, guilds, gathering, crafting and auctions remain functional. Pod health alone cannot prove gameplay compatibility.
4. **Optional only:** on a controlled restart, test `PlayerbotAutonomy.Enable = 1` and capture the startup message at INFO level. It must manipulate no bots; restore disabled config afterward.
5. Update issue #3 with the actual observations and record any unresolved licensing/revision provenance tasks separately. Do not rerun the multi-hour build merely to capture a log message.

No routine PR should trigger the prior multi-hour image build. New C++/runtime integration should instead request a separate consciously scoped full build and live test.
