# M0 licensing review — open question

This is a **source-notice audit**, not legal advice or a final licence choice.

Inspected on 15 November 2026:

| Repository | Observed notice | Implication / remaining check |
| --- | --- | --- |
| mod-playerbots/azerothcore-wotlk | Root **LICENSE** contains GNU GPL version 2 text. | Check any header-specific licence grants, exceptions and applicable version terms. |
| RovxBot/mod-playerbots | Root **LICENSE** contains GNU GPL version 2 text; individual source notices sometimes say GPLv2-or-later. | The precise version grant for each linked/copied component is not resolved by the root file alone. |
| RovxBot/mod-dungeon-clear | Root **LICENSE** says **AGPL-3.0-or-later** and asserts that mod-playerbots is AGPLv3. | That assertion is inconsistent with the playerbots fork's root GPLv2 LICENSE and needs clarification, not an unsupported assumption. |
| RovxBot/mod-playerbot-autonomy | No distribution licence has yet been chosen. | Public GitHub visibility alone does not grant redistribution rights. |

**Decision pending:** determine the actual obligations, permissions and exceptions for the intended linking model before publishing a final distribution licence or copying code from either module. Seek maintainer clarification / appropriate professional advice if necessary. Avoid importing large portions of the dependent modules; implement independent narrow interfaces and preserve attribution.

This unresolved issue is documented as part of **issue #3**. Do not silently claim GPLv2 and AGPLv3 compatibility, or resolve the conflict by assuming that the dungeon-clear licence statement overrides a different repository's actual source notices.
