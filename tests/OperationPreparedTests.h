// Included by the production-store binary MySQL harness, after its helpers.
#include "../src/OperationModule.cpp"
#include "LFGMgr.h"
#include "Operations/OperationRuntime.h"
#include "RoutineMaintenancePolicy.h"

uint64 OperationClock = 100000;
uint64 ReadOperationClock() { return OperationClock; }
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
