// Included by the production-store binary MySQL harness, after its helpers.
#include "../src/OperationModule.cpp"
#include "LFGMgr.h"
#include "Operations/OperationRuntime.h"
#include "RoutineMaintenancePolicy.h"
#if AUTONOMY_DC_BRIDGE
#include "Integration/DcAutonomyController.h"
#endif

uint64 OperationClock = 100000;
uint64 ReadOperationClock() { return OperationClock; }
// The actual scheduler/store use this value-only engine boundary. It never
// creates an AzerothCore group; the SQL and binary core decoder remain real.
struct RuntimeGroupFake final : GroupPort
{
    GroupEvidence Group;
    unsigned Creates = 0, Adds = 0, Disbands = 0, FailAdd = 0;
    bool Ungrouped = true;
    bool Colocated = false;
    ModuleDatabasePool* Database = nullptr;
    bool DelayCreationSQL = false, DelayDeletionSQL = false;
    std::vector<std::string> QueuedCoreSQL;
    void CoreWrite(std::string sql, bool delay)
    {
        if (!Database) return;
        if (delay) QueuedCoreSQL.push_back(std::move(sql)); else Database->Raw(sql);
    }
    void FlushCoreWrites()
    {
        for (auto const& sql : QueuedCoreSQL) Database->Raw(sql);
        QueuedCoreSQL.clear();
    }
    std::string Admission(Operation const&) const override { return ""; }
    GroupEvidence Inspect(uint32 id) const override { return id == Group.Id ? Group : GroupEvidence{}; }
    uint32 Create(uint32 leader) override
    {
        ++Creates; Group.Id = 991; Group.Leader = leader; Group.Members = {leader};
        if (Colocated) { Group.Map = 389; Group.Instance = 88; }
        Group.Normal = Group.SafeControl = Group.SafeToDisband = true; Ungrouped = false;
        if (Database)
        {
            CoreWrite("INSERT INTO `groups` VALUES(991,"+std::to_string(leader)+")", DelayCreationSQL);
            CoreWrite("INSERT INTO group_member(memberGuid,guid) VALUES("+std::to_string(leader)+",991)", DelayCreationSQL);
        }
        return Group.Id;
    }
    bool Add(Operation const&, RosterMember const& m) override
    {
        if (++Adds == FailAdd) return false;
        Group.Members.push_back(m.Guid);
        CoreWrite("INSERT INTO group_member(memberGuid,guid) VALUES("+std::to_string(m.Guid)+",991)", DelayCreationSQL);
        return true;
    }
    bool Disband(uint32) override
    {
        ++Disbands; Group = {}; Ungrouped = true;
        CoreWrite("DELETE FROM group_member WHERE guid=991", DelayDeletionSQL);
        CoreWrite("DELETE FROM `groups` WHERE guid=991", DelayDeletionSQL);
        return true;
    }
    bool ParticipantsUngrouped(Operation const&) const override { return Ungrouped; }
};
#if AUTONOMY_DC_BRIDGE
struct RuntimeDungeonFake final : DcAutonomy::Port
{
    unsigned Starts = 0, Stops = 0;
    bool WrongInstance = false;
    DcAutonomy::State State = DcAutonomy::State::Running;
    DcAutonomy::Permit Started;
    bool OnWorldThread() const override { return true; }
    bool Enabled() const override { return true; }
    uint64 Now() const override { return OperationClock; }
    std::string Validate(DcAutonomy::Permit const&) const override { return ""; }
    bool Initialize(DcAutonomy::Permit const& p) override { ++Starts; Started = p; return true; }
    DcAutonomy::Status Observe(DcAutonomy::Permit const& p) override
    {
        auto s = DcAutonomy::Controller::Identity(p); s.state = State; s.lastProgress = OperationClock;
        if (WrongInstance) ++s.instance;
        s.encounters = 15; s.requiredEncounters = 15;
        s.reason = "injected_engine_evidence"; return s;
    }
    bool Teardown(DcAutonomy::Permit const&) override { ++Stops; return true; }
};
inline DcAutonomy::Controller* RuntimeBridgeController = nullptr;
inline DcAutonomy::Executor const RuntimeBridgeExecutor{DcAutonomy::Version,
    [](DcAutonomy::Permit const& p) { return RuntimeBridgeController->Start(p); },
    [](DcAutonomy::Permit const& p) { return RuntimeBridgeController->Poll(p); },
    [](DcAutonomy::Permit const& p) { return RuntimeBridgeController->Stop(p); }};
#endif
Operation FixtureOperation(uint64 id)
{
    Operation op;
    op.Id = id;
    op.Guild = 23;
    op.Faction = 0;
    op.Leader = 100;
    op.CreatedAt = op.ChangedAt = OperationClock;
    op.Deadline = OperationClock + 90;
    op.LeaseUntil = OperationClock + 120;
    for (uint32 guid = 100; guid < 105; ++guid)
    {
        RosterMember m;
        m.Guid = guid;
        m.Account = 7;
        m.Guild = 23;
        m.Role = guid == 100 ? CombatRole::Tank : guid == 101 ? CombatRole::Healer : CombatRole::Dps;
        op.Members.push_back(m);
    }
    return op;
}
void TestOperations(GuildStore& guildStore, EnrollmentStore& enrollment)
{
    Runtime().Stop();
    for (auto const* table :
         {"playerbot_autonomy_operation_lease", "playerbot_autonomy_operation_member",
          "playerbot_autonomy_operation_journal", "playerbot_autonomy_operation", "playerbot_autonomy_guild_audit",
          "playerbot_autonomy_guild", "playerbot_autonomy_recruitment_audit", "playerbot_autonomy_recruitment",
          "playerbot_autonomy_enrollment_audit", "playerbot_autonomy_transition_receipt",
          "playerbot_autonomy_enrollment", "guild_member", "guild"})
        guildStore.Raw(std::string("DELETE FROM ") + table);
    FakeGuildMgr.Guilds.clear();
    FakeCharacterCache.Entries.clear();
    FakeOtherPlayers.clear();
    FakePlayer.Online = false;
    guildStore.Raw("UPDATE characters SET account=7 WHERE guid BETWEEN 100 AND 179");
    char const* names[] = {"Ashen Vanguard", "Amber Covenant", "Silver Horizon", "Dawn Wardens"};
    for (uint32 i = 0; i < 4; ++i)
    {
        uint32 guild = 23 + i, founder = 100 + 20 * i;
        auto intent = StagedIntent(guildStore, enrollment, i + 1, founder, 7, names[i]);
        intent.Phase = GuildPhase::Creating;
        assert(Await(guildStore.Save(intent, GuildPhase::Planned)));
        guildStore.Raw("INSERT INTO guild VALUES(" + std::to_string(guild) + "," + std::to_string(founder) + ",'" +
                       names[i] + "')");
        guildStore.Raw("INSERT INTO guild_member VALUES(" + std::to_string(founder) + "," + std::to_string(guild) +
                       ")");
        intent.Guild = guild;
        intent.Phase = GuildPhase::Managed;
        assert(Await(guildStore.Save(intent, GuildPhase::Creating)));
        EnrollmentRecord before{founder, 7, guild, 1, EnrollmentState::Pending, true, true};
        TransitionRequest confirm{founder,
                                  7,
                                  guild,
                                  1,
                                  EnrollmentRequest::ConfirmAfterGuildJoin,
                                  GuildRequestId(founder, guild, 0x1002, 1),
                                  "fixture confirm"};
        assert(Await(enrollment.Commit(confirm, before)));
        auto worldGuild = std::make_unique<Guild>();
        worldGuild->Id = guild;
        worldGuild->Leader = founder;
        worldGuild->Name = names[i];
        for (uint32 guid = founder; guid < founder + 20; ++guid)
        {
            if (guid != founder)
            {
                guildStore.Raw("INSERT INTO guild_member VALUES(" + std::to_string(guid) + "," + std::to_string(guild) +
                               ")");
                guildStore.Raw(
                    "INSERT INTO playerbot_autonomy_enrollment(character_guid,account_id,state,revision) VALUES(" +
                    std::to_string(guid) + ",7,'PERSISTENT',2)");
            }
            FakeCharacterCache.Entries[guid] = {7, guild, 1, {}, "Fixture", 0};
            Player p;
            p.Guid = guid;
            p.Guild = guild;
            p.AI.Tank = guid == founder;
            p.AI.Healer = guid == founder + 1;
            p.AI.Dps = !p.AI.Tank && !p.AI.Healer;
            FakeOtherPlayers.emplace(guid, p);
            worldGuild->Members.emplace(guid, Guild::Member{});
        }
        FakeGuildMgr.AddGuild(worldGuild.release());
    }
    auto identities =
        guildStore.Inspect("SELECT GROUP_CONCAT(CONCAT(guildid,':',leaderguid,':',name) ORDER BY guildid) FROM guild")
            ->Fetch()[0]
            .Get<std::string>();
    auto intents = guildStore
                       .Inspect(
                           "SELECT GROUP_CONCAT(CONCAT(slot,':',phase,':',name,':',changed_at) ORDER BY slot) "
                           "FROM playerbot_autonomy_guild")
                       ->Fetch()[0]
                       .Get<std::string>();
    OperationStore store;
    assert(store.Initialize("disposable"));
    auto op = FixtureOperation(9007199254740993ULL);
    assert(Await(store.Save(op, nullptr)));
    assert(!Await(store.Save(op, nullptr)));
    Operation decoded;
    uint64 leases = 0;
    assert(OperationStore::Decode(Await(store.Load(op.Id)), decoded, leases) && SameOperation(op, decoded) && !leases);
    store.Raw("DELETE FROM playerbot_autonomy_execution WHERE operation_id=" + std::to_string(op.Id));
    assert(!OperationStore::Decode(Await(store.Load(op.Id)), decoded, leases));
    store.Raw("INSERT INTO playerbot_autonomy_execution VALUES(" + std::to_string(op.Id) + ",1,0,0,0,0,0,0)");
    auto validated = NextOperation(op, OperationClock + 1, true, true);
    assert(Await(store.Save(validated, &op)));
    auto reserved = NextOperation(validated, OperationClock + 2, true, true);
    FakeLostCommitAcknowledgement = true;
    assert(!Await(store.Save(reserved, &validated)));
    FakeLostCommitAcknowledgement = false;
    assert(OperationStore::Decode(Await(store.Load(op.Id)), decoded, leases) && SameOperation(reserved, decoded) &&
           leases == 5);
    assert(!Await(store.Save(reserved, &validated)));  // uncertain retry leaves exactly five leases
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_journal") == 3);
    auto collision = FixtureOperation(2);
    assert(Await(store.Save(collision, nullptr)));
    auto cv = NextOperation(collision, OperationClock + 1, true, true);
    assert(Await(store.Save(cv, &collision)));
    auto cr = NextOperation(cv, OperationClock + 2, true, true);
    assert(!Await(store.Save(cr, &cv)));
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 5);
    auto assembling = NextOperation(reserved, OperationClock + 3, true, true);
    assert(Await(store.Save(assembling, &reserved)));
    auto activeBeforeCrash = NextOperation(assembling, OperationClock + 4, true, true);
    assert(Await(store.Save(activeBeforeCrash, &assembling)));
    // Restart recovery runs with M2 off and needs no live player/map facts.
    OperationSettings settings;
    OperationRuntime runtime(ReadOperationClock);
    runtime.Configure(settings);
    runtime.Start("disposable");
    for (unsigned i = 0; i < 80; ++i)
        runtime.Update(100);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation WHERE state='CANCELLED'") == 2);
    assert(runtime.Operations().empty());
    runtime.Stop();
    runtime.Stop();
    Runtime().Configure(true);
    Runtime().Start("disposable");
    GuildSettings gs;
    gs.Enabled = true;
    gs.DryRun = true;
    gs.Target = 4;
    gs.MaxMembers = 20;
    Guilds().Configure(gs);
    Guilds().Start("disposable");
    PumpGuild(Guilds(), 300);  // Initial 8,250-character fixture needs 65 asynchronous pages.
    assert(Guilds().Ready());
    settings.Enabled = true;
    settings.IntervalMs = 1000;
    settings.RefreshBudget = 16;
    runtime.Configure(settings);
    runtime.Start("disposable");
    std::size_t peak = 0;
    auto pump = [&](unsigned ticks)
    {
        for (unsigned i = 0; i < ticks; ++i)
        {
            ++OperationClock;
            Runtime().Update(1000);
            Guilds().Update(1000);
            auto jobs = runtime.ScheduledJobs();
            runtime.Update(1000);
            assert(runtime.ScheduledJobs() - jobs <= 1);
            peak = std::max(peak, runtime.Operations().size());
            assert(runtime.Operations().size() <= settings.MaxConcurrent);
        }
    };
    pump(16);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation") == 2);  // default dry run
    assert(runtime.Diagnose(23).find("plan=eligible") != std::string::npos);
    // Real adapter role evidence, queues, control and profession capabilities.
    auto m = runtime.Observe(100, 23, true);
    assert(m.Role == CombatRole::Tank && m.EquipmentKnown && m.Professions.at(164) == 225);
    FakeOtherPlayers.at(100).Talents = {{0, 10}, {1, 10}, {2, 0}};
    assert(runtime.Observe(100, 23, true).Role == CombatRole::Unknown);
    FakeOtherPlayers.at(100).Talents = {{0, 31}, {1, 0}, {2, 0}};
    FakeOtherPlayers.at(100).AI.Healer = true;
    assert(runtime.Observe(100, 23, true).Role == CombatRole::Unknown);
    FakeOtherPlayers.at(100).AI.Healer = false;
    FakeLfg.Queued.insert(100);
    assert(runtime.Observe(100, 23, true).Queued);
    FakeLfg.Queued.clear();
    FakeOtherPlayers.at(100).Selfbot = true;
    assert(runtime.Observe(100, 23, true).HumanControlled);
    FakeOtherPlayers.at(100).Selfbot = false;
    settings.DryRun = false;
    runtime.Configure(settings);
    FakeLostCommitAcknowledgement = true;
    pump(60);
    FakeLostCommitAcknowledgement = false;
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation WHERE state='COMPLETED'") > 0);
    settings.MaxConcurrent = 4;
    settings.TimeoutSeconds = 300;
    settings.LeaseSeconds = 360;
    runtime.Configure(settings);
    pump(100);
    assert(peak == 4);
    // A delayed acknowledgement retains the local batch, then expires without
    // exposing the character to maintenance. Readback still closes durably.
    for (unsigned i = 0; i < 100 && runtime.Operations().empty(); ++i)
        pump(1);
    assert(!runtime.Operations().empty());
    auto active = runtime.Operations().begin()->second;
    assert(Runtime().Cache().OperationOwner(active.Leader) == active.Id);
    for (auto action :
         {Playerbots::RoutineMaintenance::Action::Randomize, Playerbots::RoutineMaintenance::Action::LevelReset,
          Playerbots::RoutineMaintenance::Action::RandomTeleport, Playerbots::RoutineMaintenance::Action::GroupTeardown,
          Playerbots::RoutineMaintenance::Action::PopulationTrim})
        assert(Playerbots::RoutineMaintenance::ShouldBlockRoutineMaintenance(active.Leader, action));
    FakeDatabaseReady = false;
    // Backward wall-clock changes cannot extend the bounded local lease.
    OperationClock -= 200;
    runtime.Update(400000);
    assert(!Runtime().Cache().OperationOwner(active.Leader));
    assert(Runtime().Cache().ShouldBlock(active.Leader));
    FakeDatabaseReady = true;
    settings.Enabled = false;
    runtime.Configure(settings);
    pump(40);
    assert(runtime.Operations().empty());
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
    runtime.Stop();
    // Command permission/parser checks are read-only and cannot write operations.
    {
        // This harness has no core Group/TC9 headers; missing real capability
        // must refuse admission before acquiring any lease.
        assert(!CoreGroups().Available());
        OperationRuntime missing(ReadOperationClock);
        auto unavailable = settings;
        unavailable.Enabled = true; unavailable.AssembleGroups = true; unavailable.DryRun = false;
        missing.Configure(unavailable); missing.Start("disposable");
        for (unsigned n = 0; n < 20; ++n) missing.Update(1000);
        assert(missing.Operations().empty());
        assert(missing.Diagnose(0).find("core_group_adapter_unavailable") != std::string::npos);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
        missing.Stop();
    }
#if AUTONOMY_DC_BRIDGE
    {
        for (auto& item : FakeOtherPlayers)
        { item.second.Level = 17; item.second.AI.ItemLevel = 180; }
        RuntimeGroupFake groups; groups.Database = &store; groups.DelayCreationSQL = true;
        OperationRuntime real(ReadOperationClock, &groups);
        auto realSettings = settings;
        realSettings.Enabled = true; realSettings.DryRun = false; realSettings.AssembleGroups = true;
        realSettings.TrustedDungeon = true; realSettings.MaxConcurrent = 1;
        realSettings.PilotProfile = 1; realSettings.RealmMaxExpansion = 1;
        realSettings.PilotGuild = 23;
        realSettings.AssemblySeconds = 300;
        real.Configure(realSettings); real.Start("disposable");
        auto tick = [&]
        {
            ++OperationClock; Runtime().Update(1000); Guilds().Update(1000);
            auto jobs = real.ScheduledJobs(); real.Update(1000); assert(real.ScheduledJobs() - jobs <= 1);
        };
        FakeLostCommitAcknowledgement = true;
        for (unsigned n = 0; n < 180 && (!groups.Creates || real.Operations().empty() ||
             !real.Operations().begin()->second.Group); ++n) tick();
        FakeLostCommitAcknowledgement = false;
        assert(groups.Creates == 1 && groups.Adds == 4 && real.Operations().size() == 1);
        auto owned = real.Operations().begin()->second;
        assert(owned.Execution == ExecutionMode::RealGroup && owned.Group == 991 && owned.CreationFenced &&
               owned.State == OperationState::Assembling);
        for (unsigned n = 0; n < 10; ++n) tick();
        assert(real.Operations().begin()->second.State != OperationState::Active);
        assert(!real.Operations().begin()->second.GroupConfirmed);
        groups.FlushCoreWrites(); groups.DelayCreationSQL = false;
        for (unsigned n = 0; n < 40 && !real.Operations().begin()->second.GroupConfirmed; ++n) tick();
        assert(real.Operations().begin()->second.GroupConfirmed);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 5);
        FakeDatabaseReady = false;
        real.Update(5000000);
        assert(Runtime().Cache().OperationOwner(owned.Leader) == owned.Id);
        assert(Runtime().Cache().ShouldBlock(owned.Leader));
        FakeDatabaseReady = true;
        realSettings.Enabled = false; real.Configure(realSettings);
        groups.DelayDeletionSQL = true;
        Runtime().Configure(false); // Master activities off; M1 protection stays on.
        for (unsigned n = 0; n < 220 && !real.Operations().empty(); ++n) tick();
        assert(!real.Operations().empty() && groups.Disbands == 1);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 5);
        assert(Runtime().Cache().OperationOwner(owned.Leader) == owned.Id);
        groups.FlushCoreWrites(); groups.DelayDeletionSQL = false;
        for (unsigned n = 0; n < 60 && !real.Operations().empty(); ++n) tick();
        assert(real.Operations().empty() && groups.Creates == 1 && groups.Disbands == 1);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
        std::cout << "MEASURE real scheduler with fake engine and actual SQL: " << real.Diagnose(0) << '\n';
        real.Stop();
        Runtime().Configure(true);
        PumpGuild(Guilds(), 300);
        assert(Guilds().Ready());
        {
            RuntimeGroupFake partial; partial.Database = &store; partial.FailAdd = 2;
            OperationRuntime rollback(ReadOperationClock, &partial);
            auto rollbackSettings = realSettings; rollbackSettings.Enabled = true;
            rollback.Configure(rollbackSettings); rollback.Start("disposable");
            for (unsigned n = 0; n < 200 && (!partial.Disbands || !rollback.Operations().empty()); ++n)
            {
                ++OperationClock; Runtime().Update(1000); Guilds().Update(1000); rollback.Update(1000);
            }
            assert(partial.Creates == 1 && partial.Adds == 2 && partial.Disbands == 1);
            assert(rollback.Operations().empty());
            assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
            rollback.Stop();
            std::cout << "PASS production partial assembly compensation: two persisted members, core SQL proof, "
                         "one disband, durable delete confirmation before five-lease release\n";
        }

#if AUTONOMY_DC_BRIDGE
        // Containment of entry failures by the EXISTING runtime. There is no
        // portal controller here: map/instance/exit evidence is injected.
        for (unsigned scenario = 0; scenario < 4; ++scenario)
        {
            RuntimeDungeonFake dungeon;
            DcAutonomy::Controller provider(dungeon);
            RuntimeBridgeController = &provider;
            assert(DcAutonomy::RegisterExecutor(&RuntimeBridgeExecutor));
            RuntimeGroupFake entry; entry.Database = &store;
            OperationRuntime guarded(ReadOperationClock, &entry);
            auto scoped = realSettings; scoped.Enabled = true;
            guarded.Configure(scoped); guarded.Start("disposable");
            auto entryTick = [&]
            {
                ++OperationClock; Runtime().Update(1000); Guilds().Update(1000); guarded.Update(1000);
            };
            for (unsigned n = 0; n < 240 && (guarded.Operations().empty() ||
                 !guarded.Operations().begin()->second.GroupConfirmed); ++n) entryTick();
            assert(guarded.Operations().size() == 1 && entry.Creates == 1);
            auto held = guarded.Operations().begin()->second;
            assert(held.GroupConfirmed && held.State == OperationState::Assembling && !dungeon.Starts);
            entry.Group.SafeToDisband = false;
            // 0: portal denied/no member entered, 1: partial entry/separated
            // members (CoreGroupPort collapses colocation to map/instance 0),
            // 2: cancellation during entry, 3: executor instance mismatch.
            if (scenario == 3)
            {
                entry.Group.Map = 389; entry.Group.Instance = 88;
                for (unsigned n = 0; n < 40 && !dungeon.Starts; ++n) entryTick();
                assert(dungeon.Starts == 1);
                dungeon.WrongInstance = true;
            }
            else
            {
                entry.Group.Map = entry.Group.Instance = 0;
                if (scenario == 2)
                {
                    scoped.KillSwitch = true;
                    guarded.Configure(scoped);
                }
                else
                    OperationClock = held.CreatedAt + scoped.AssemblySeconds;
            }
            for (unsigned n = 0; n < 40; ++n) entryTick();
            assert(guarded.Operations().size() == 1);
            auto closing = guarded.Operations().begin()->second;
            assert(closing.State == OperationState::Closing && !closing.Succeeded && !entry.Disbands);
            assert(dungeon.Starts == (scenario == 3 ? 1u : 0u));
            assert(dungeon.Stops == (scenario == 3 ? 1u : 0u));
            auto status = guarded.Diagnose(0);
            auto reason = scenario <= 1 ? "real_assembly_or_colocation_timeout" : scenario == 2 ?
                "real_feature_disabled" : "executor_observation_identity_mismatch";
            assert(status.find(reason) != std::string::npos);
            // Cleanup can take longer than the activity deadline. No expiry,
            // cancellation, master switch or login allowance releases ownership.
            scoped.Enabled = false; guarded.Configure(scoped);
            OperationClock = std::max(OperationClock, held.LeaseUntil + 100);
            for (unsigned n = 0; n < 20; ++n) entryTick();
            assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 5);
            for (auto guid : ParticipantGuids(held))
            {
                assert(Runtime().Cache().OperationOwner(guid) == held.Id);
                Runtime().Cache().AllowLogin(guid); // Enrollment handoff cannot bypass an operation lease.
                assert(Runtime().Cache().ShouldBlock(guid) && Runtime().Cache().LoginBlocked(guid));
                assert(!Runtime().Cache().AcquireOperation(held.Id + 1, {guid}));
            }
            // Authoritative ordinary recovery/exit is a test input, NOT an
            // action performed by this candidate. Durable absence is required.
            entry.Group.SafeToDisband = true;
            entry.Group.Map = entry.Group.Instance = 0;
            entry.DelayDeletionSQL = true;
            for (unsigned n = 0; n < 40; ++n) entryTick();
            assert(entry.Disbands == 1 && !guarded.Operations().empty());
            assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 5);
            entry.FlushCoreWrites(); entry.DelayDeletionSQL = false;
            for (unsigned n = 0; n < 80 && !guarded.Operations().empty(); ++n) entryTick();
            assert(guarded.Operations().empty() && entry.Disbands == 1);
            assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
            for (auto guid : ParticipantGuids(held))
                assert(!Runtime().Cache().OperationOwner(guid) && Runtime().Cache().ShouldBlock(guid));
            guarded.Stop(); RuntimeBridgeController = nullptr;
        }
        std::cout << "PASS entry failure containment with actual SQL/controllers: failed portal/separated wait "
                     "timeout, cancellation, instance mismatch, exclusive leases past expiry, M1 retained, "
                     "SQL-confirmed cleanup; entry/recovery/exit evidence is injected, no movement executed\n";

        // Both actual modules' production controllers and real SQL interact;
        // their engine ports inject group/instance/run evidence explicitly.
        {
            RuntimeDungeonFake dungeon;
            DcAutonomy::Controller provider(dungeon);
            RuntimeBridgeController = &provider;
            assert(DcAutonomy::RegisterExecutor(&RuntimeBridgeExecutor));
            RuntimeGroupFake colocated; colocated.Colocated = true; colocated.Database = &store;
            OperationRuntime combined(ReadOperationClock, &colocated);
            auto combinedSettings = realSettings; combinedSettings.Enabled = true;
            combined.Configure(combinedSettings); combined.Start("disposable");
            assert(combined.Diagnose(24).find("outside_pilot_guild") != std::string::npos);
            auto combinedTick = [&]
            {
                ++OperationClock; Runtime().Update(1000); Guilds().Update(1000); combined.Update(1000);
            };
            FakeLostCommitAcknowledgement = true;
            for (unsigned n = 0; n < 200 && (!dungeon.Starts || combined.Operations().empty() ||
                 combined.Operations().begin()->second.State != OperationState::Active); ++n) combinedTick();
            FakeLostCommitAcknowledgement = false;
            assert(dungeon.Starts == 1 && colocated.Creates == 1 && combined.Operations().size() == 1 &&
                   combined.Operations().begin()->second.State == OperationState::Active);
            auto exactPermit = dungeon.Started;
            assert(provider.Start(exactPermit).state == DcAutonomy::State::NotStarted && dungeon.Starts == 1);
            colocated.Group.SafeToDisband = false;
            dungeon.State = DcAutonomy::State::Completed;
            for (unsigned n = 0; n < 20; ++n) combinedTick();
            assert(combined.Operations().size() == 1 &&
                   combined.Operations().begin()->second.State == OperationState::Closing &&
                   combined.Operations().begin()->second.Succeeded && dungeon.Stops == 1 && !colocated.Disbands);
            assert(Runtime().Cache().OperationOwner(exactPermit.leader) == exactPermit.operation);
            // Fake authoritative normal exit, not a teleport/action by autonomy.
            colocated.Group.SafeToDisband = true; colocated.Group.Map = colocated.Group.Instance = 0;
            combinedSettings.Enabled = false; combined.Configure(combinedSettings);
            for (unsigned n = 0; n < 60 && !combined.Operations().empty(); ++n) combinedTick();
            assert(combined.Operations().empty() && colocated.Disbands == 1 && dungeon.Stops == 1);
            assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
            assert(provider.Start(exactPermit).state == DcAutonomy::State::NotStarted && dungeon.Starts == 1);
            combined.Stop(); RuntimeBridgeController = nullptr;
            std::cout << "PASS matched production autonomy/DC controllers + actual SQL: authentic permit, "
                         "single launch despite lost ACK, real-mode ACTIVE only after executor ACK, terminal evidence, "
                         "instance-safe retained leases, confirmed cleanup; engine run/exit evidence is injected\n";
        }
#endif

        // A half-assembled durable real operation never resumes or takes over
        // an extant group after restart. Its exact leases remain protected.
        auto half = FixtureOperation(9007199254741999ULL);
        half.Execution = ExecutionMode::RealGroup;
        assert(Await(store.Save(half, nullptr)));
        auto advance = [&](OperationState state)
        {
            auto before = half; half.State = state; ++half.Revision; ++half.Attempts; ++half.ChangedAt;
            assert(Await(store.Save(half, &before)));
        };
        advance(OperationState::Validated); advance(OperationState::Reserved);
        half.CreationFenced = true; half.Group = 991; advance(OperationState::Assembling);
        groups.Group = {}; groups.Group.Id = 991; groups.Group.Leader = half.Leader;
        groups.Group.Members = ParticipantGuids(half);
        groups.Group.Normal = groups.Group.SafeControl = groups.Group.SafeToDisband = true;
        groups.Ungrouped = false;
        real.Start("disposable");
        for (unsigned n = 0; n < 80; ++n) tick();
        assert(!real.Operations().empty() && groups.Disbands == 1);
        assert(real.Diagnose(0).find("manual_restart_group_ownership_reconciliation") != std::string::npos);
        assert(Runtime().Cache().OperationOwner(half.Leader) == half.Id);
        // External operator cleanup is represented by authoritative absence;
        // the scheduler itself never disbands this recovered group.
        groups.Group = {}; groups.Ungrouped = true;
        store.Raw("DELETE FROM group_member WHERE guid=991"); store.Raw("DELETE FROM `groups` WHERE guid=991");
        for (unsigned n = 0; n < 80 && !real.Operations().empty(); ++n) tick();
        assert(real.Operations().empty() && groups.Disbands == 1);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
        real.Stop();
        std::cout << "PASS production real scheduler/store: creation fence/receipt, lost acknowledgement, "
                     "no simulated ACTIVE, unsafe-expiry protection, disabled cleanup, half-group restart manual gate; "
                     "group APIs are faked\n";
    }
#else
    {
        RuntimeGroupFake groups;
        OperationRuntime missingProfile(ReadOperationClock, &groups);
        auto unavailable = settings;
        unavailable.Enabled = true; unavailable.DryRun = false; unavailable.AssembleGroups = true;
        unavailable.PilotProfile = 1; unavailable.RealmMaxExpansion = 1;
        unavailable.PilotGuild = 23;
        missingProfile.Configure(unavailable); missingProfile.Start("disposable");
        for (unsigned n = 0; n < 80; ++n)
        {
            ++OperationClock; Runtime().Update(1000); Guilds().Update(1000); missingProfile.Update(1000);
        }
        assert(groups.Creates == 0 && missingProfile.Operations().empty());
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_operation_lease") == 0);
        missingProfile.Stop();
        std::cout << "PASS missing/incompatible profile provider: no real admission, groups or leases\n";
    }
#endif
    AutonomyOperationCommands commands;
    auto handler = commands.GetCommands().at(0).Handler;
    ChatHandler console;
    assert(handler(&console, "plan 23"));
    assert(!handler(&console, "plan -1"));
    console.Session = &FakePlayer.Session;
    assert(!handler(&console, "status"));
    console.Session = nullptr;
    FakeConfig.Values["Ra.MinLevel"] = 2;
    assert(handler(&console, "status"));
    assert(console.Messages.back().find("administrator-only") != std::string::npos);
    FakeConfig.Values["Ra.MinLevel"] = 3;
    assert(
        guildStore.Inspect("SELECT GROUP_CONCAT(CONCAT(guildid,':',leaderguid,':',name) ORDER BY guildid) FROM guild")
            ->Fetch()[0]
            .Get<std::string>() == identities);
    assert(guildStore
               .Inspect("SELECT GROUP_CONCAT(CONCAT(slot,':',phase,':',name,':',changed_at) ORDER BY slot) "
                        "FROM playerbot_autonomy_guild")
               ->Fetch()[0]
               .Get<std::string>() == intents);
    Guilds().Stop();
    Runtime().Stop();
    FakeOtherPlayers.clear();
    FakeCharacterCache.Entries.clear();
    std::cout << "PASS production operation store/runtime: uint64 identity, atomic lease conflict, lost/late ack, "
                 "disabled restart recovery, real adapter evidence, expiry/cleanup, four guild identities unchanged\n";
}
