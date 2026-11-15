"""Self-contained fixture tests for the integration source-shape checker."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_integration_contracts import validate  # noqa: E402


class IntegrationContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        folder = Path(self.tempdir.name)
        self.core = folder / "core"
        self.bots = folder / "playerbots"
        self.dc = folder / "dungeon-clear"
        fixtures = [
            (self.core, {
                "src/cmake/ac_macros.cmake": "MACRO(AC_ADD_SCRIPT path)\nENDMACRO()\n",
                "modules/CMakeLists.txt": (
                    "set(LOADER_FUNCTION AddModuleScripts())\nLOCALE_SCRIPT_MODULE\n"
                    "file(GLOB module_config conf/*.conf.dist)\nCopyModuleConfig(x)\n"
                ),
                "src/server/game/Scripting/ScriptDefines/WorldScript.h": (
                    "WORLDHOOK_ON_STARTUP\nvirtual void OnStartup() {}\n"
                ),
                "src/common/Configuration/Config.h": "GetOption(\n",
            }),
            (self.bots, {
                "src/Bot/RandomPlayerbotMgr.cpp": (
                    "RandomPlayerbotMgr::ProcessBot\nRandomPlayerbotMgr::Refresh\n"
                ),
            }),
            (self.dc, {
                "src/mod_dungeon_clear_loader.cpp": "void Addmod_dungeon_clearScripts() {}\n",
            }),
        ]
        for base, files in fixtures:
            for relative, content in files.items():
                target = base / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content, encoding="utf-8")

    def test_valid_contract(self) -> None:
        self.assertEqual(validate(self.core, self.bots, self.dc, ROOT), [])

    def test_missing_core_loader_contract_fails(self) -> None:
        (self.core / "modules/CMakeLists.txt").write_text("CopyModuleConfig(x)\n", encoding="utf-8")
        problems = validate(self.core, self.bots, self.dc, ROOT)
        self.assertTrue(any("core generated loader" in x for x in problems))

    def test_missing_dependency_source_fails(self) -> None:
        (self.bots / "src/Bot/RandomPlayerbotMgr.cpp").unlink()
        problems = validate(self.core, self.bots, self.dc, ROOT)
        self.assertTrue(any("cannot read" in x for x in problems))


if __name__ == "__main__":
    unittest.main()
