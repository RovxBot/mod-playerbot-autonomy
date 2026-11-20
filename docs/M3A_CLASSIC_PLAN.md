# Classic pilot implementation plan — 20 November 2026

Scope: issue #9, the trusted boundary in #8, and the supplied M3A Classic brief.
No realm actions, infrastructure access, production SQL or combined build.

Source bases: autonomy `a5630a6`; owner dungeon-clear `565086c` (the merged
`f9201249e287027f0d393da17eb85ef588c49847` tree); owner playerbots
`696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`; existing read-only core validation
snapshot `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`. Ordinary local playerbots
is older; use the existing detached `.autonomy-m3a-playerbots` checkout.
No changes to playerbots or core are planned.

1. Audit RFC's normal boss discovery, ordering, routing, access and completion.
   Core SQL defines kill encounters 430–433 for Oggleflint (11517), Taragaman
   (11520), Jergosh (11518), Bazzalan (11519), with normal spawns 48644, 48706,
   48740, 48750. The instance script is deliberately empty. DC's BossSpawnIndex
   joins loaded encounter definitions to static spawns and orders by the loaded
   DBC index; NextDungeonBossValue filters completed bits/corpses and uses the
   ordinary navigation/engage path. RFC already has a tested nav penalty wall.
   No RFC-specific event or boss-state slot exists or is needed in this source.
2. Add a versioned, single allowlisted normal RFC profile to the trusted API.
   Bind the profile and conservative realm expansion cap into permits. Validate
   actual loaded encounter mappings and DC boss anchors; derive the required
   mask from those mappings, never from UK's three-bit constant. Refuse partial
   previous clears. Keep existing authorization and normal access hooks intact.
3. Consume the profile for planning, assembly and launch. Require explicit
   profile opt-in, preserve all M1/control/ownership/lease/cleanup gates, reject
   unsupported providers, unknown profiles and any Wrath expansion cap. Clamp
   pilot characters to the realm's Classic/TBC level ceiling. No schema change:
   one immutable profile, and restart groups always cancel/reconcile.
4. Extend actual-controller and core-adapter tests in C++17/C++20, exercise the
   production stores/controllers on isolated MySQL 8, then run pinned-header
   syntax, source contracts and scaffold checks. Add a short operator runbook
   and record exact final dependency commits and validation limits.

Thread/performance boundary remains the world update barrier after map workers
join. Profile/encounter validation examines four definitions and five members;
no DB queries, realm scans, movement implementation or reward provisioning are
added. Existing asynchronous SQL receipts retain ownership until safe cleanup.

Local DBC extracts are unavailable: source SQL alone does not establish bit
indices. Runtime validation must resolve and check them from loaded primary
game data; missing or mismatched definitions block launch. Navmesh reachability,
masterless timing, ordinary portal entry and safe exit remain live pilot gates.
RFC is source-suitable; no Stockade fallback is selected.
