#!/usr/bin/env python3
"""A lightweight structural guard, not an AzerothCore compilation test."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
required = [
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
