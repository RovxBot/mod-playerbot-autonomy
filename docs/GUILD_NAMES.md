# Curated guild naming

New founding intents select from the exact 46 supplied candidates in
`src/Guild/GuildNamePool.h`. Spelling, case, punctuation and Unicode are retained;
only leading/trailing ASCII whitespace is trimmed. There are no faction themes,
slot suffixes or founder-name fallbacks. Existing intents, including Ashen
Vanguard, Amber Covenant, Silver Horizon and Dawn Wardens, retain their names.
No migration or guild identity rewrite is included.

The world adapter calls the actual core `ObjectMgr::IsValidCharterName` before
selection, and again through the existing create gate. It checks core guilds
with `GuildMgr::GetGuildByName` and all persisted founding intents with the
core's case-insensitive comparator. The existing unique SQL name key remains
the final atomic collision guard (its Unicode collation can be stricter than
the core comparator). A database-only collation conflict fails closed and needs
operator diagnosis; it cannot rename an existing intent. No founding SQL,
ownership proof, recovery transaction, recruitment or enrollment logic changes.

All pool candidates are evaluated for read-only rejection diagnostics. Selection
is deterministic: first valid unused candidate in supplied order. The chosen
name is persisted in the existing PLANNED intent **before** staging/creation.
Existing intent progression never calls selection, so retries/restarts cannot
replace its name. If all names are invalid/used, founding reports
`guild_name_pool_exhausted` without reserving a founder or inventing a fallback.
A name acquired by another guild after intent persistence is rejected by the
existing final core check; the intent remains unchanged for safe review.

Console/administrator-only RA:

```
autonomy-guild names
```

This reports the next available candidate and every rejected name with the core
constraint or collision reason. It does not write/queue work. Ownership must
already be loaded through the existing guild runtime; otherwise it reports
unavailable evidence. The normal founding/director switches are unchanged.

## Pinned core validation evidence

Core test-staging `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`;
playerbots master `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`.
`tools/check_guild_names.py` executes the production rejection adapter against
small unmodified functions extracted from that core: `IsValidCharterName`,
`isValidString`, `GetRealmLanguageType`, and `Utf8toWStr`. It uses the core's
actual Unicode predicates, UTF-8 library, realm-zone enum and charter length.
Only configuration values and reserved/profanity sets are test fixtures.
It does not compile ObjectMgr/worldserver or import implementation into the
module. This follows the existing licence audit's no-copy boundary.

With **MinCharterName=2, StrictCharterNames=0 and empty word-filter sets**, 44
candidates are accepted. Two remain in the pool but are rejected:

| Candidate | Exact rejection |
| --- | --- |
| `CyLx-亚服` | `core_disallowed_character=U+002D` (hyphen) |
| `*The Un*named GDKP` | `core_disallowed_character=U+002A` (asterisk) |

These are fixture results, not observations of production configuration or
word-filter data. Additional runtime rejections include minimum length,
reserved/profane names, mixed scripts or realm-language rules. In particular,
`XS`/`ED` need a minimum of two characters, and Cyrillic/East Asian candidates
need an allowing strict-name/realm policy. The adapter never transliterates or
bypasses those rules. Tests also exercise invalid UTF-8, 25-character names,
mixed scripts, strict Latin, Russian realm rules and populated word filters.

The candidate hash test checks all 46 entries against the original task, not
just the examples. C++17/C++20 tests cover trimming, duplicates, unchanged
existing intents, restart and exhausted pools. The binary MySQL harness calls
production `GuildStore::Save` for all 44 accepted fixture names, reconnects and
verifies exact Unicode strings; a case-insensitive duplicate rolls back without
an extra receipt. Existing founding/recovery/recruitment suites remain required.
Real-header syntax includes the new validation adapter.

A combined worldserver build/link and live name selection remain future gates.
No image build, production DB access/write, deployment, guild rename, server
restart or merge is part of this change. No repeat M1/founding pilot is required.
