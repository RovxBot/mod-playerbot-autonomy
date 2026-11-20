#!/usr/bin/env python3
"""Exercise production stores/runtime and pinned core result decoding on disposable MySQL.

No CMake or worldserver build. Compiles only this isolated test executable plus
the core's unmodified Field.cpp/QueryResult.cpp. Async/engine boundaries are test
ports; SQL, Store parameter calls and core binary result decoding are real.
Requires PyMySQL, MySQL client development files and the pinned core checkout.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE_PIN = "0554bcdd664ff6d85291ca2a10fd31d3e37e89e6"
PLAYERBOTS_PIN = "696a009ceeeb400cfe40eedcd8a6c9333bdc1b59"
DC_PIN = "a6627e6ad05445737090b41618346088e8637e1d"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--playerbots", type=Path, required=True)
    parser.add_argument("--dungeon-clear", type=Path)
    parser.add_argument("--mysql-dir", type=Path)
    parser.add_argument("--boost-include", type=Path, default=Path("/usr/include"))
    args = parser.parse_args()
    pins = [(args.core, CORE_PIN), (args.playerbots, PLAYERBOTS_PIN)]
    if args.dungeon_clear:
        pins.append((args.dungeon_clear, DC_PIN))
    for path, pin in pins:
        sha = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if sha != pin:
            parser.error(f"Expected pinned dependency {pin}, found {sha} at {path}")
    if not (os.getenv("AUTONOMY_MYSQL_SOCKET") or os.getenv("AUTONOMY_MYSQL_HOST")):
        parser.error("Explicit disposable MySQL connection required")
    sys.path.insert(0, str(ROOT / "tests"))
    from test_enrollment_mysql import EnrollmentMySQLTests

    # The existing fixture checks the disposable database name and MySQL 8
    # version before applying replayable migrations to minimal public tables.
    EnrollmentMySQLTests.setUpClass()
    connection = EnrollmentMySQLTests.connection
    try:
        with connection.cursor() as cur:
            for name in ("playerbot_autonomy_guild_audit", "playerbot_autonomy_guild",
                         "playerbot_autonomy_recruitment_audit", "playerbot_autonomy_recruitment",
                         "playerbot_autonomy_enrollment_audit", "playerbot_autonomy_transition_receipt",
                         "playerbot_autonomy_enrollment", "guild_member", "group_member", "characters"):
                cur.execute(f"DELETE FROM {name}")
            cur.execute("CREATE TABLE IF NOT EXISTS guild (guildid INT UNSIGNED PRIMARY KEY, "
                        "leaderguid INT UNSIGNED, name VARCHAR(24)) ENGINE=InnoDB")
            cur.execute("DELETE FROM guild")
            cur.executemany("INSERT INTO characters(guid,account) VALUES(%s,7)", [(i,) for i in range(1, 301)])
            cur.executemany("INSERT INTO playerbot_autonomy_enrollment(character_guid,account_id,state,revision) "
                            "VALUES(%s,7,%s,%s)", [(188, "PENDING", 17), (189, "PERSISTENT", 9007199254740993),
                                                   (190, "RETIRED", 19), (191, "PENDING", 18446744073709551614)])
        connection.commit()
    finally:
        EnrollmentMySQLTests.tearDownClass()

    database = args.core.resolve() / "src/server/database/Database"
    headers = {p.parent for root in (args.core / "src/common", args.core / "src/server/database")
               for p in root.rglob("*.h")}
    includes = [ROOT / "tests/prepared_mysql", ROOT / "tests/runtime_fakes", ROOT / "src",
                args.playerbots / "src/Bot", args.boost_include, args.core / "deps/fmt/include",
                args.core / "deps/SFMT", *sorted(headers)]
    if args.dungeon_clear:
        includes.append(args.dungeon_clear.resolve() / "src")
    if args.mysql_dir:
        includes.insert(0, args.mysql_dir / "include")
        mysql_flags = [f"-L{args.mysql_dir / 'lib'}", f"-Wl,-rpath,{args.mysql_dir / 'lib'}", "-lmysqlclient"]
    else:
        mysql_flags = shlex.split(subprocess.check_output(["mysql_config", "--libs"], text=True))
        mysql_flags += shlex.split(subprocess.check_output(["mysql_config", "--cflags"], text=True))
    with tempfile.TemporaryDirectory(prefix="autonomy-prepared-") as tmp:
        build = Path(tmp)
        (build / "CoreQueryResult.h").write_text(f'#include "{database / "QueryResult.h"}"\n')
        sources = [ROOT / "tests/PreparedMySQLTests.cpp", ROOT / "src/Persistence/EnrollmentStore.cpp",
                   ROOT / "src/Operations/OperationStore.cpp", ROOT / "src/Operations/OperationCoordinator.cpp",
                   ROOT / "src/Operations/OperationRuntime.cpp", ROOT / "src/Operations/RosterPlanner.cpp",
                   ROOT / "src/Operations/CoreGroupPort.cpp",
                   ROOT / "src/Persistence/EnrollmentRuntime.cpp", ROOT / "src/Persistence/EnrollmentTransition.cpp",
                   ROOT / "src/Guild/GuildStore.cpp", ROOT / "src/Guild/GuildDirector.cpp",
                   ROOT / "src/Guild/GuildRuntime.cpp",
                   ROOT / "src/Guild/RecruitmentDirector.cpp", database / "Field.cpp", database / "QueryResult.cpp"]
        # The pinned core uses std::type_identity. Its decoder requires C++20;
        # module-only policies/controllers remain covered in C++17 and C++20.
        command = [os.getenv("CXX", "c++"), "-std=c++20", "-O0", "-g", "-pthread", "-include", "cstring",
                   "-DAUTONOMY_PREPARED_MYSQL", f"-I{build}", *[f"-I{p.resolve()}" for p in includes],
                   *map(str, sources), *mysql_flags, "-o", str(build / "prepared-tests")]
        print(f"Core {CORE_PIN}; playerbots {PLAYERBOTS_PIN}; DC {DC_PIN if args.dungeon_clear else 'unavailable'}; C++20 decoder harness", flush=True)
        subprocess.run(command, check=True)
        subprocess.run([str(build / "prepared-tests")], check=True)


if __name__ == "__main__":
    main()
