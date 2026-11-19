#!/usr/bin/env python3
"""A lightweight structural guard, not an AzerothCore compilation test."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
required = [
    "src/Guild/GuildNamePool.h",
    "src/Guild/GuildNameValidation.cpp",
    "tests/GuildNameTests.cpp",
    "tools/check_guild_names.py",
    "docs/GUILD_NAMES.md",
    "src/OperationModule.cpp",
    "src/Operations/OperationRuntime.cpp",
    "src/Operations/CoreGroupPort.cpp",
    "src/Operations/RealGroupExecutor.h",
    "src/Operations/DungeonBridge.h",
    "tests/RealGroupTests.cpp",
    "tests/CoreGroupPortTests.cpp",
    "data/sql/db-characters/2026_11_19_02_playerbot_autonomy_execution.sql",
    "docs/M3A_RELEASE.md",
    "src/Operations/OperationStore.cpp",
    "src/Operations/OperationCoordinator.cpp",
    "src/Operations/RosterPlanner.cpp",
    "data/sql/db-characters/2026_11_19_01_playerbot_autonomy_operations.sql",
    "tests/OperationTests.cpp",
    "tests/OperationPreparedTests.h",
    "docs/M2_OPERATIONS.md",
    "data/sql/db-characters/2026_11_19_00_playerbot_autonomy_guild_journal.sql",
    "data/sql/db-characters/2026_11_18_00_playerbot_autonomy_guild.sql",
    "src/GuildModule.cpp",
    "src/Guild/GuildDirector.cpp",
    "src/Guild/GuildRuntime.cpp",
    "src/Guild/GuildStore.cpp",
    "data/sql/db-characters/2026_11_18_01_playerbot_autonomy_recruitment.sql",
    "src/Guild/RecruitmentDirector.cpp",
    "AGENTS.md",
    "README.md",
    "CMakeLists.txt",
    "conf/mod_playerbot_autonomy.conf.dist",
    "src/mod_playerbot_autonomy_loader.cpp",
    "src/AutonomyModule.cpp",
    "src/Policy/PopulationPolicy.h",
    "src/Persistence/EnrollmentState.h",
    "data/sql/db-characters/2026_11_15_00_playerbot_autonomy_enrollment.sql",
    "tests/EnrollmentStateTests.cpp",
    "tests/test_enrollment_schema.py",
    "tests/PopulationPolicyTests.cpp",
    "docs/ARCHITECTURE.md",
    "docs/ROADMAP.md",
    "docs/ENROLLMENT.md",
    "docs/INTEGRATION.md",
    "docs/LICENSING.md",
    "tools/check_integration_contracts.py",
    "tests/test_integration_contracts.py",
]
for name in required:
    assert (root / name).is_file(), f"Missing required scaffold file: {name}"

cmake = (root / "CMakeLists.txt").read_text()
loader = (root / "src/mod_playerbot_autonomy_loader.cpp").read_text()
config = (root / "conf/mod_playerbot_autonomy.conf.dist").read_text()
script = (root / "src/AutonomyModule.cpp").read_text()

assert 'AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")' in cmake
assert "AC_ADD_CONFIG_FILE(" not in cmake
assert "Addmod_playerbot_autonomyScripts()" in loader
assert "AddSC_playerbot_autonomy_module()" in loader
assert "void AddSC_playerbot_autonomy_module()" in script
assert "PlayerbotAutonomy.Enable = 0" in config
assert 'GetOption<bool>("PlayerbotAutonomy.Enable", false)' in script
print("Module scaffold checks passed (no AzerothCore build performed).")
