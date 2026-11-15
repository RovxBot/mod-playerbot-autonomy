#!/usr/bin/env python3
"""Read-only, inexpensive source-shape check for a pinned playerbots core stack.

Not a C++ compile, link, startup test or forward-compatibility guarantee.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def validate(core: Path, playerbots: Path, dungeon_clear: Path, autonomy: Path) -> list[str]:
    errors: list[str] = []

    def source(root: Path, relative: str) -> str:
        path = root / relative
        try:
            return path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"cannot read {path}: {exc}")
            return ""

    def expect(name: str, content: str, token: str) -> None:
        if token not in content:
            errors.append(f"{name}: missing expected token: {token!r}")

    macros = source(core, "src/cmake/ac_macros.cmake")
    modules = source(core, "modules/CMakeLists.txt")
    world_script = source(core, "src/server/game/Scripting/ScriptDefines/WorldScript.h")
    core_config = source(core, "src/common/Configuration/Config.h")
    bot_mgr = source(playerbots, "src/Bot/RandomPlayerbotMgr.cpp")
    dc_loader = source(dungeon_clear, "src/mod_dungeon_clear_loader.cpp")
    local_cmake = source(autonomy, "CMakeLists.txt")
    local_loader = source(autonomy, "src/mod_playerbot_autonomy_loader.cpp")
    local_script = source(autonomy, "src/AutonomyModule.cpp")
    local_conf = source(autonomy, "conf/mod_playerbot_autonomy.conf.dist")

    expect("core script registration", macros, "MACRO(AC_ADD_SCRIPT path)")
    expect("core generated loader", modules, "LOADER_FUNCTION")
    expect("core generated loader", modules, "LOCALE_SCRIPT_MODULE")
    expect("core automatic config installation", modules, "CopyModuleConfig(")
    expect("core automatic config discovery", modules, "*.conf.dist")
    expect("core startup hook", world_script, "WORLDHOOK_ON_STARTUP")
    expect("core startup callback", world_script, "virtual void OnStartup()")
    expect("core configuration access", core_config, "GetOption(")

    # Boundary checks only; do not assert future autonomy APIs exist.
    expect("playerbots population manager", bot_mgr, "RandomPlayerbotMgr::ProcessBot")
    expect("playerbots maintenance manager", bot_mgr, "RandomPlayerbotMgr::Refresh")
    expect("dungeon-clear entry point", dc_loader, "Addmod_dungeon_clearScripts()")

    expect("autonomy source registration", local_cmake, "AC_ADD_SCRIPT(")
    if "AC_ADD_CONFIG_FILE(" in local_cmake:
        errors.append("autonomy CMake calls deprecated/no-op AC_ADD_CONFIG_FILE")
    expect("autonomy generated entry point", local_loader, "Addmod_playerbot_autonomyScripts()")
    expect("autonomy script registration", local_loader, "AddSC_playerbot_autonomy_module()")
    expect("autonomy script definition", local_script, "void AddSC_playerbot_autonomy_module()")
    expect("autonomy startup hook", local_script, "WORLDHOOK_ON_STARTUP")
    expect("autonomy default-off config read", local_script,
           'GetOption<bool>("PlayerbotAutonomy.Enable", false)')
    if not re.search(r"(?m)^\s*PlayerbotAutonomy\.Enable\s*=\s*0\s*$", local_conf):
        errors.append("autonomy config must default PlayerbotAutonomy.Enable to 0")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--playerbots", type=Path, required=True)
    parser.add_argument("--dungeon-clear", type=Path, required=True)
    parser.add_argument("--autonomy", type=Path, default=Path("."))
    args = parser.parse_args()
    problems = validate(args.core, args.playerbots, args.dungeon_clear, args.autonomy)
    for problem in problems:
        print(f"FAIL: {problem}")
    if problems:
        return 1
    print("PASS: pinned source-shape contracts (no AzerothCore build or startup performed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
