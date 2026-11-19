#!/usr/bin/env python3
"""Syntax-check real adapter/module sources against local pinned dependency headers.

No CMake, object files, linking, startup or deployment. Supply installed Boost and
MySQL development headers. The pinned core itself requires the GNU C++20 dialect.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--playerbots", type=Path, required=True)
    parser.add_argument("--boost-include", type=Path, default=Path("/usr/include"))
    parser.add_argument("--mysql-include", type=Path, default=Path("/usr/include/mysql"))
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--standard", default="gnu++20")
    parser.add_argument("--mode", choices=("autonomy", "playerbots", "both"), default="both")
    args = parser.parse_args()
    core, playerbots = args.core.resolve(), args.playerbots.resolve()
    autonomy = Path(__file__).resolve().parents[1]
    headers = sorted({p.parent for root in (core / "src", playerbots / "src") for p in root.rglob("*.h")})
    includes = (
        [args.boost_include, args.mysql_include, autonomy / "src"]
        + [
            core / path
            for path in (
                "deps/g3dlite/include",
                "deps/recastnavigation/Detour/Include",
                "deps/fmt/include",
                "deps/SFMT",
            )
        ]
        + headers
    )
    flags = [f"-std={args.standard}", "-fsyntax-only", "-DMOD_PLAYERBOTS"] + [f"-I{path}" for path in includes]
    files: list[Path] = []
    if args.mode in ("autonomy", "both"):
        files += [
            autonomy / path
            for path in (
                "src/AutonomyModule.cpp",
                "src/GuildModule.cpp",
                "src/Guild/GuildDirector.cpp",
                "src/Guild/GuildNameValidation.cpp",
                "src/Guild/RecruitmentDirector.cpp",
                "src/Guild/GuildStore.cpp",
                "src/Guild/GuildRuntime.cpp",
                "src/Persistence/EnrollmentStore.cpp",
                "src/Persistence/EnrollmentRuntime.cpp",
                "src/Persistence/EnrollmentTransition.cpp",
            )
        ]
    if args.mode in ("playerbots", "both"):
        files += [
            playerbots / path
            for path in (
                "src/Bot/RandomPlayerbotMgr.cpp",
                "src/Bot/RandomBotLevelMgr.cpp",
                "src/Bot/Factory/PlayerbotFactory.cpp",
                "src/Bot/Factory/RandomPlayerbotFactory.cpp",
                "src/Bot/PlayerbotMgr.cpp",
                "src/Bot/PlayerbotAI.cpp",
                "src/Mgr/Guild/PlayerbotGuildMgr.cpp",
                "src/Ai/Base/Actions/TrainerAction.cpp",
            )
        ]
    failed = False
    for file in files:
        result = subprocess.run([args.cxx, *flags, str(file)], check=False)
        print(f"{'PASS' if result.returncode == 0 else 'FAIL'} syntax: {file}", flush=True)
        failed |= result.returncode != 0
    print("Syntax checks only; no worldserver compilation, linking or startup.")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
