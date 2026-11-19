#!/usr/bin/env python3
"""Execute the production name adapter with pinned actual core name functions.

Extracts small unmodified functions into a disposable harness; does not build
ObjectMgr or the worldserver. Config, reserved/profanity sets are fixtures.
Actual UTF-8 conversion, Unicode predicates and charter/language rules are core
source. The generated excerpts never become distributed module implementation.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

PIN = '0554bcdd664ff6d85291ca2a10fd31d3e37e89e6'
ROOT = Path(__file__).resolve().parents[1]

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--boost-include', type=Path, default=Path('/usr/include'))
    args = parser.parse_args()
    core = args.core.resolve()
    sha = subprocess.check_output(['git','-C',str(core),'rev-parse','HEAD'],text=True).strip()
    if sha != PIN:
        parser.error(f'Expected core {PIN}, got {sha}')
    obj = (core/'src/server/game/Globals/ObjectMgr.cpp').read_text()
    obj_header = (core/'src/server/game/Globals/ObjectMgr.h').read_text()
    util = (core/'src/common/Utilities/Util.cpp').read_text()
    world_header = (core/'src/server/game/World/World.h').read_text()
    realm_zones = re.search(r'enum RealmZone\s*\{[^}]*\};',world_header,re.S).group(0)
    maximum = re.search(r'#define MAX_CHARTER_NAME\s+(\d+)',obj_header).group(1)
    language = re.search(r'enum LanguageType\s*\{[^}]*\};',obj,re.S).group(0)
    includes = sorted({p.parent for p in (core/'src').rglob('*.h')})
    with tempfile.TemporaryDirectory(prefix='autonomy-core-names-') as directory:
        tmp = Path(directory)
        (tmp/'World.h').write_text('''#pragma once
#include "SharedDefines.h"
'''+realm_zones+'''
enum { CONFIG_MIN_CHARTER_NAME, CONFIG_STRICT_CHARTER_NAMES, CONFIG_REALM_ZONE };
struct World {
 uint32 Minimum=2, Strict=0, Zone=REALM_ZONE_UNITED_STATES;
 uint32 getIntConfig(int key) const { return key==CONFIG_MIN_CHARTER_NAME ? Minimum : key==CONFIG_STRICT_CHARTER_NAMES ? Strict : Zone; }
};
inline World NameWorld;
#define sWorld (&NameWorld)
''')
        (tmp/'ObjectMgr.h').write_text('''#pragma once
#include <string>
#include <string_view>
#include <set>
#include "Define.h"
#define MAX_CHARTER_NAME '''+maximum+'''
struct ObjectMgr {
 static bool IsValidCharterName(std::string_view);
 std::set<std::string> Reserved, Profane;
 bool IsReservedName(std::string_view name) const { return Reserved.count(std::string(name)); }
 bool IsProfanityName(std::string_view name) const { return Profane.count(std::string(name)); }
};
inline ObjectMgr NameObjectMgr;
#define sObjectMgr (&NameObjectMgr)
''')
        source = '''#include <cassert>
#include <iostream>
#include <iterator>
#include <utf8.h>
#include "ObjectMgr.h"
#include "World.h"
#include "Util.h"
#include "Guild/GuildNamePool.h"
'''
        source += language+'\n'
        for signature in ('static LanguageType GetRealmLanguageType(bool create)',
                          'bool isValidString(std::wstring wstr,',
                          'bool ObjectMgr::IsValidCharterName(std::string_view name)'):
            source += function(obj,signature)+'\n'
        source += function(util,'bool Utf8toWStr(std::string_view utf8str, std::wstring& wstr)')+'\n'
        source += r'''
int main() {
 using namespace PlayerbotAutonomy;
 unsigned rejected=0;
 for (auto name : GuildNamePool) {
  auto reason=CoreGuildNameRejection(name);
  assert(reason.empty()==ObjectMgr::IsValidCharterName(name));
  if (!reason.empty()) ++rejected;
  std::cout << name << '\t' << (reason.empty()?"accepted":reason) << '\n';
 }
 assert(rejected==2);
 assert(CoreGuildNameRejection("CyLx-亚服")=="core_disallowed_character=U+002D");
 assert(CoreGuildNameRejection("*The Un*named GDKP")=="core_disallowed_character=U+002A");
 assert(CoreGuildNameRejection("NЖ")=="core_mixed_script");
 assert(CoreGuildNameRejection(std::string(25,'A'))=="core_max_characters=24 actual=25");
 assert(CoreGuildNameRejection("\xff")=="core_invalid_utf8");
 NameWorld.Minimum=3;
 assert(CoreGuildNameRejection("XS")=="core_min_characters=3 actual=2");
 NameWorld.Minimum=2; NameWorld.Strict=1;
 assert(CoreGuildNameRejection("暗流")=="core_realm_language_policy strict_mask=1");
 NameWorld.Strict=2; NameWorld.Zone=REALM_ZONE_RUSSIAN;
 assert(CoreGuildNameRejection("ЦентуриТрайб").empty());
 assert(!CoreGuildNameRejection("暗流").empty());
 NameWorld.Strict=0;
 NameObjectMgr.Reserved.insert("Numen");
 assert(CoreGuildNameRejection("Numen")=="core_reserved_name");
 NameObjectMgr.Profane.insert("ffs");
 assert(CoreGuildNameRejection("ffs")=="core_profanity_name");
 std::cout << "PASS production adapter + actual core UTF8/charter/language rules; 44 accepted, 2 rejected in min=2 strict=0 empty word-filter fixture\n";
}
'''
        # Raw source keeps literal UTF-8 and C++ escapes; normalize the delimiter
        # escapes in generated output only.
        source = source.replace("'\\\\t'", "'\\t'").replace("'\\\\n'", "'\\n'")
        source = source.replace('"\\\\xff"', '"\\xff"')
        (tmp/'names.cpp').write_text(source)
        command = ['c++','-std=gnu++20','-Wall','-Wextra','-Werror',f'-I{tmp}',
                   f'-I{ROOT / "src"}', f'-I{args.boost_include}',f'-I{core / "deps/utf8cpp"}',
                   f'-I{core / "deps/fmt/include"}',f'-I{core / "deps/SFMT"}',
                   *[f'-I{p}' for p in includes],str(tmp/'names.cpp'),
                   str(ROOT/'src/Guild/GuildNameValidation.cpp'),'-o',str(tmp/'names')]
        subprocess.run(command,check=True)
        subprocess.run([str(tmp/'names')],check=True)
    print(f'Core {PIN}; no worldserver build/link/startup; realm config/word filters remain runtime inputs.')

if __name__ == '__main__':
    main()
