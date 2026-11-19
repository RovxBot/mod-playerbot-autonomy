# M3A evidence and implementation plan

This is source work, not server or dungeon acceptance. No image build,
deployment, live SQL, character changes or infrastructure edits are authorised.

## Resolved sources

- Autonomy merged main: `f2f27cbc1b4a79d076ae261973bdf6a0db043aaf`.
- Dungeon-clear owner master base: `7f1600bacfc282a8b9407a4089407747bdce1480`;
  provider `c074510f8c928f99694a161f5d5705b732ca7d29`.
- Playerbots owner master: `696a009ceeeb400cfe40eedcd8a6c9333bdc1b59`.
- Core test-staging validation snapshot: `0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`.

The historical image manifest did not record resolved dependency SHAs. These
validation snapshots must not be described as the exact deployed source set.
The read-only infrastructure checkout `62434d34376f1cc63da53f23360467a24588dfd7`
still clones the core/modules by branch in
`.github/workflows/azerothcore-wotlk-images.yml:89` and `:119`, without a resolved
source manifest there. Its inputs name core test-staging, owner playerbots
master, owner dungeon-clear master and autonomy main. The core validation
checkout's tracked `origin/test-staging` also resolves to `0554bcdd...`; this
does not prove which dependency commits a previously deployed image contains.
The ordinary local core/playerbots checkouts were older/incompatible snapshots;
validation uses the explicit snapshots above. Infrastructure consultation is
read-only. Issues #7, #8, #9, #13 and #29 were read before implementation.

## Verified boundaries

Core `Group::Create(Player*)` creates a normal group, adds its leader, inherits
the leader's difficulty and queues character SQL. `sGroupMgr->AddGroup` registers
it. `Group::AddMember` updates character-cache membership and queues SQL, but
also calls `Player::ResetInstances(INSTANCE_RESET_GROUP_JOIN)` for followers.
Assembly must reject existing instance bindings and incompatible difficulty
before invoking it. `Disband` deletes the object; it is a no-op in Cloud9
cluster mode. Never dereference a group after disband. Cluster mode is outside
this executor's supported boundary. Group identity alone is insufficient proof
of ownership: validate the leader, authoritative member slots and live control.

Core `World::Update` invokes world scripts after `MapMgr::Update` waits for map
workers. Engine observations/mutations occur only there, never in SQL callbacks
executed by a database worker. Keep value identities, not engine pointers.

`DcOnAction::Execute` checks `IsAuthorized` before leader election, dungeon and
boss checks, strategy activation, prior-run teardown, navigation/pull resets,
wing selection and status publication. Extract initialization behind that
unchanged chat check. Trusted starts call the shared implementation directly
after provider validation, without creating an Event owner. `DcRun` is leader
owned; followers use `DcLeaderSignal`. `DisableDungeonClear` is the shared
terminal teardown and already reports `kReasonAllCleared` to the test observer.
An independent observer must preserve terminal evidence across its reset.

The GM harness provisions characters and sets a GM master. It is unsuitable.
RDF/BG managers own separate jobs and must remain isolated. Normal masterless
playerbots omit default `avoid aoe`; their reaction delay is also slower.
Scoped strategy activation is possible without changing world-filler defaults.

Masterless source audit (all paths relative to the pinned owner checkout):

| Concern | Verified path and M3A consequence |
| --- | --- |
| Tank | `Util/DcLeaderSignal.cpp::FindLeaderTank` elects a living bot tank; the bridge additionally requires the selected tank and actual group leader to agree. No replacement tank is provisioned on death. |
| Followers | `Value/DungeonClearPartyTankValue.cpp::Calculate` resolves the elected tank only while enabled/unpaused; `DungeonClearStrategy.cpp` installs follow/assist/hold actions. No human master is needed for those signals. |
| Combat/healing | The existing `dungeon clear combat` strategy supplies combat assist/reposition triggers. `DungeonClearMultiplier.cpp` suppresses follow-master movement for active members and leaves healer healing out of the DPS threat hold. Existing class rotations/heals remain in playerbots. |
| AoE/timing | Playerbots `AiFactory.cpp` master-gates default `avoid aoe`; the bridge scopes its addition/restoration. `PlayerbotAI.cpp::GetReactDelay` keeps its slower masterless policy; timing needs a pilot or a separately reviewed compatibility API. |
| Loot | The existing DC loot filter/roll triggers and playerbots loot collection remain; no loot, XP or currency is created by this bridge. Reward retention is a live acceptance gate. |
| Resurrection | `DcRezRecovery` elects normal resurrection spells. M3A live validation treats death/control loss as cancellation and suppresses the privileged entrance regroup path. It does not synthesize resurrection/progression. |
| Death/logout | `FindRunOwner` can find an enabled dead tank, whereas normal leader election excludes it. The bridge tracks the exact original GUID/nonce and never converts tank loss into success. Missing owner/stop evidence holds leases for reconciliation. |
| RDF/GM isolation | `DcDungeonQueueFillJob::ClaimPoolCharacter` delegates to `DcPoolBots` and its own held roster; GM jobs use `DcTestRunManager` reservations. Autonomy selects dedicated random accounts through M1 and rejects queued/grouped/human-master bots. Neither manager is called or patched by the new provider. |

For normal groups, core `PlayerbotHolder::LogoutPlayerBot` and the existing
maintenance provider remain authoritative; M3A neither suppresses real client
logout nor retains Player/Group pointers. Core group writes use asynchronous
`CHAR_INS_GROUP`, `CHAR_REP_GROUP_MEMBER`, `CHAR_DEL_GROUP` and
`CHAR_DEL_GROUP_MEMBER_ALL`. New module readbacks confirm creation before
launch/teardown and deletion before terminal release, using indexed identities.

Dungeon-clear contains privileged recovery paths (stranded teleport, wipe
entrance teleport, combat purge and breath-item provisioning). Autonomous runs
must suppress those paths, including operator overrides, and stop on unsupported
recovery. Normal player/GM runs retain their existing policies.

## Plan and verification

1. Add a default-off, versioned dungeon-clear provider; bind single-use permits
   to provider identity, operation revision, roster, group, map, instance and
   expiry. Share manual initialization and teardown; retain terminal evidence.
2. Extend the existing coordinator with explicit real execution, a durable
   creation fence/receipt, authoritative group checks and conservative cleanup.
   SQL uncertainty never repeats group creation. Restart cancels, never resumes.
3. Admit one audited normal target only. Travel/entry is a separate M3B gate;
   group assembly and co-located dungeon execution are independently testable.
4. Run C++17/C++20 policies and production controllers with engine fakes,
   disposable MySQL 8.0/8.4 transactions and pinned real-header syntax checks.
   Record scheduler/query costs and explicitly separate these from linking,
   gameplay, reward retention and soak evidence.
5. Push companion and consumer PRs, then update owner-repository issues. Do not
   close #9 or wait idly for asynchronous CI.
