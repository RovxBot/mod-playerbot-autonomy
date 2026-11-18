// Production stores/runtime, actual pinned core Field/PreparedResultSet, real
// binary prepared MySQL. Only engine objects and queue scheduling are faked.
#include <cctype>
#include <limits>
#include <thread>

#include "../src/GuildModule.cpp"
#include "CoreFakes.h"
#include "Guild/GuildRuntime.h"
#include "Guild/GuildStore.h"
#include "GuildFakes.h"
#include "Persistence/EnrollmentRuntime.h"
#include "Persistence/EnrollmentSql.h"

using namespace PlayerbotAutonomy;

// Field.cpp's case-insensitive alias helper; no gameplay/common library link.
bool StringEqualI(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
PreparedQueryResult Await(QueryCallback callback)
{
    PreparedQueryResult result;
    callback.WithPreparedCallback([&](PreparedQueryResult value) { result = std::move(value); });
    assert(callback.InvokeIfReady());
    assert(result);
    return result;
}
bool Await(TransactionCallback callback)
{
    bool result = false;
    callback.AfterComplete([&](bool value) { result = value; });
    assert(callback.InvokeIfReady());
    return result;
}
uint64 Scalar(ModuleDatabasePool& pool, std::string const& sql) { return pool.Inspect(sql)->Fetch()[0].Get<uint64>(); }
void Pump(unsigned count = 25)
{
    for (unsigned i = 0; i < count; ++i)
        Runtime().Update(100);
}
void CheckSnapshot(EnrollmentRuntime& runtime = Runtime())
{
    auto const& cache = runtime.Cache();
    assert(cache.Ready());
    auto absent = cache.Get(187);
    assert(absent.IdentityValid && absent.State == EnrollmentState::Absent && absent.Revision == 0);
    assert(!cache.ShouldBlock(187));
    assert(cache.Get(188).State == EnrollmentState::Pending && cache.Get(188).Revision == 17);
    assert(cache.Get(189).State == EnrollmentState::Persistent && cache.Get(189).Revision == 9007199254740993ULL);
    assert(cache.Get(190).State == EnrollmentState::Retired && cache.Get(190).Revision == 19);
    assert(cache.Get(191).State == EnrollmentState::Pending && cache.Get(191).Revision == 18446744073709551614ULL);
    assert(cache.ShouldBlock(188) && cache.ShouldBlock(189));
    for (uint32 guid : {1, 128, 129, 256, 257, 300})
        assert(cache.Get(guid).IdentityValid && cache.Get(guid).Revision == 0);
}
void TestEnrollment(EnrollmentStore& store)
{
    // Negative control: execute the pre-fix production expression through the
    // core binary result buffer. Decimal ASCII '0' is not uint64 binary zero.
    std::string legacy = Sql::Page;
    auto const at = legacy.find("CAST(0 AS UNSIGNED)");
    assert(at != std::string::npos);
    legacy.replace(at, std::string("CAST(0 AS UNSIGNED)").size(), "0");
    for (auto pos = legacy.find('?'); pos != std::string::npos; pos = legacy.find('?'))
        legacy.replace(pos, 1, "128");
    auto bad = store.Inspect(legacy);
    auto* fields = bad->Fetch();
    assert(fields[5].GetType() == DatabaseFieldTypes::Decimal);
    assert(fields[5].Get<std::string>() == "0");
    auto const garbage = fields[5].Get<uint64>();
    assert(garbage != 0 && (garbage & 0xff) == 0x30);
    std::cout << "REPRO legacy revision metadata=NEWDECIMAL text=0 core uint64=" << garbage << '\n';

    auto page = Await(store.LoadPage(186, 1));
    fields = page->Fetch();
    assert(fields[0].Get<uint32>() == 187 && fields[5].GetType() == DatabaseFieldTypes::Int64);
    assert(fields[5].Get<uint64>() == 0);
    assert(fields[2].GetType() == DatabaseFieldTypes::Int32 && fields[2].IsNull());
    assert(fields[6].GetType() == DatabaseFieldTypes::Int32 && fields[6].IsNull());

    FakePlayer.Guid = 187;
    FakePlayer.Online = false;
    Runtime().Configure(true);
    Runtime().Start("disposable");
    Pump();
    CheckSnapshot();
    auto diagnosis = Runtime().DiagnoseStage(187);
    assert(diagnosis.Passed() && diagnosis.Cached.Revision == 0);
    auto const initialPages = Runtime().Counters().Pages;
    for (unsigned i = 0; i < 4; ++i)
    {
        Runtime().Update(60000);
        Pump();
        CheckSnapshot();
    }
    assert(Runtime().Counters().Pages >= initialPages + 12);
    Runtime().Stop();
    Runtime().Configure(false);  // maintenance protection survives disabled activities and restart
    Runtime().Start("disposable");
    Pump();
    CheckSnapshot();

    store.Raw("UPDATE playerbot_autonomy_enrollment SET revision=0 WHERE character_guid=188");
    Runtime().Configure(false);
    Pump();
    assert(!Runtime().Cache().Ready() && Runtime().Cache().ShouldBlock(187));
    assert(Runtime().Cache().ShouldBlock(188) && Runtime().Cache().ShouldBlock(189));
    assert(!Runtime().Submit({}));
    store.Raw("UPDATE playerbot_autonomy_enrollment SET revision=17 WHERE character_guid=188");
    Runtime().Update(5000);
    Pump();
    CheckSnapshot();
    Runtime().Stop();
    EnrollmentRuntime cold;
    cold.Configure(false);
    assert(!cold.Cache().Ready() && cold.Cache().ShouldBlock(187));
    assert(cold.Cache().Get(189).State == EnrollmentState::Unknown);
    cold.Start("disposable");
    for (unsigned i = 0; i < 25; ++i)
        cold.Update(100);
    CheckSnapshot(cold);
    cold.Stop();

    // Actual Store::Commit bindings and receipt decoding, including nullable
    // INT guild/actor values and full-width nonzero transition revisions.
    EnrollmentRecord before{187, 7, 0, 0, EnrollmentState::Absent, true, true};
    TransitionRequest request{187,
                              7,
                              0,
                              0,
                              EnrollmentRequest::StageBeforeGuildJoin,
                              "00000000-0000-0000-0000-000000000001",
                              "prepared regression"};
    assert(Await(store.Commit(request, before)));
    page = Await(store.LoadPage(186, 1));
    assert(page->Fetch()[5].Get<uint64>() == 1);
    auto receipt = Await(store.FindReceipt(request.RequestId));
    assert(receipt->Fetch()[5].GetType() == DatabaseFieldTypes::Int32);
    assert(receipt->Fetch()[7].GetType() == DatabaseFieldTypes::Int32);
    assert(receipt->Fetch()[5].Get<uint32>() == 0 && receipt->Fetch()[7].Get<uint32>() == 0);
    assert(receipt->Fetch()[9].Get<uint64>() == 0 && receipt->Fetch()[10].Get<uint64>() == 1);
    store.Raw("INSERT INTO guild_member VALUES(187,7)");
    before.State = EnrollmentState::Pending;
    before.Revision = 1;
    request.Action = EnrollmentRequest::ConfirmAfterGuildJoin;
    request.ExpectedRevision = 1;
    request.Guild = 7;
    request.ActorGuid = 187;
    request.RequestId.back() = '2';
    assert(Await(store.Commit(request, before)));
    page = Await(store.LoadPage(186, 1));
    assert(page->Fetch()[4].Get<std::string>() == "PERSISTENT" && page->Fetch()[5].Get<uint64>() == 2);
    receipt = Await(store.FindReceipt(request.RequestId));
    assert(receipt->Fetch()[5].Get<uint32>() == 7 && receipt->Fetch()[7].Get<uint32>() == 187);
    store.Raw("DELETE FROM guild_member WHERE guid=187");
    std::cout << "PASS enrollment binary decoding, 300-character pages, repeated sweeps, restart, invalid revision "
                 "protection\n";
}
void TestGuild(GuildStore& store, EnrollmentStore& enrollment)
{
    GuildIntent intent;
    intent.Slot = 3;
    intent.Founder = 200;
    intent.Account = 7;
    intent.Faction = 1;
    intent.StageRevision = 1234567890123ULL;  // deliberately different from identity
    intent.StageRequest = "00000000-0000-0000-0000-000000000003";
    intent.Name = "Prepared Regression";
    intent.Reason = "prepared binding regression";
    assert(Await(store.Save(intent, std::nullopt)));
    auto rows = Await(store.Load());
    auto* f = rows->Fetch();
    assert(f[3].GetType() == DatabaseFieldTypes::Int8 && f[3].Get<uint8>() == 1);
    assert(f[4].GetType() == DatabaseFieldTypes::Int32 && f[4].IsNull() && f[4].Get<uint32>() == 0);
    assert(f[8].Get<uint64>() == intent.StageRevision);
    assert(f[10].GetType() == DatabaseFieldTypes::Int64 && f[10].Get<uint64>() == 0);
    enrollment.Raw(
        "INSERT INTO playerbot_autonomy_enrollment(character_guid,account_id,state,revision) "
        "VALUES(200,7,'RETIRED',1234567890123)");
    EnrollmentRecord before{200, 7, 0, intent.StageRevision, EnrollmentState::Retired, true, true};
    TransitionRequest stage{200,
                            7,
                            0,
                            intent.StageRevision,
                            EnrollmentRequest::StageBeforeGuildJoin,
                            intent.StageRequest,
                            "director founding slot 3"};
    stage.Director = true;
    stage.ActorGuid = 200;
    assert(Await(enrollment.Commit(stage, before)));
    assert(Await(store.Load())->Fetch()[10].Get<uint64>() == 1);
    intent.Phase = GuildPhase::Creating;
    for (bool wrongFounder : {true, false})
    {
        auto wrong = intent;
        if (wrongFounder)
            ++wrong.Founder;
        else
            ++wrong.Account;
        assert(!Await(store.Save(wrong, GuildPhase::Planned)));
        assert(store.LastFailure.find("chk_autonomy_guild_mutation") != std::string::npos);
        assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == 1);
        assert(Await(store.Load())->Fetch()[5].Get<std::string>() == "PLANNED");
    }
    // A successful mutation followed by an audit uniqueness failure also rolls
    // back the phase; the existing receipt cannot be rewritten by a retry.
    store.Raw(
        "CREATE TRIGGER autonomy_test_audit_fail BEFORE INSERT ON playerbot_autonomy_guild_audit "
        "FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected audit failure'");
    assert(!Await(store.Save(intent, GuildPhase::Planned)));
    assert(Await(store.Load())->Fetch()[5].Get<std::string>() == "PLANNED");
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == 1);
    store.Raw("DROP TRIGGER autonomy_test_audit_fail");
    assert(Await(store.Save(intent, GuildPhase::Planned)));
    std::vector<TestStatement::Value> const expected{std::string("CREATING"),
                                                     uint32(0),
                                                     intent.Reason,
                                                     intent.Slot,
                                                     intent.Founder,
                                                     intent.Account,
                                                     std::string("PLANNED"),
                                                     std::string("CREATING"),
                                                     uint32(0)};
    assert(store.LastChange.Values == expected);
    rows.reset();
    store.Close();
    assert(store.Initialize("disposable"));
    assert(Await(store.Load())->Fetch()[5].Get<std::string>() == "CREATING");
    intent.Phase = GuildPhase::Managed;
    intent.Guild = 77;
    assert(!Await(store.Save(intent, GuildPhase::Creating)));  // missing actual guild cannot be adopted
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == 2);
    store.Raw("INSERT INTO guild VALUES(77,200,'Prepared Regression')");
    store.Raw("INSERT INTO guild_member VALUES(200,77)");
    assert(Await(store.Save(intent, GuildPhase::Creating)));
    std::vector<TestStatement::Value> const managed{std::string("MANAGED"),
                                                    uint32(77),
                                                    intent.Reason,
                                                    intent.Slot,
                                                    intent.Founder,
                                                    intent.Account,
                                                    std::string("CREATING"),
                                                    std::string("MANAGED"),
                                                    uint32(77)};
    assert(store.LastChange.Values == managed);
    assert(!Await(store.Save(intent, GuildPhase::Creating)));  // stale retry cannot create a receipt
    assert(!Await(store.Save(intent, std::nullopt)));  // unique founder/name/slot prevent duplicate create intent
    rows = Await(store.Load());
    assert(rows->Fetch()[5].Get<std::string>() == "MANAGED" && rows->Fetch()[4].Get<uint32>() == 77);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == 3);
    RecruitmentIntent recruit;
    recruit.Guid = 201;
    recruit.Account = 7;
    recruit.Guild = 77;
    recruit.Slot = 3;
    recruit.Actor = 200;
    recruit.StageRequest = "00000000-0000-0000-0000-000000000004";
    recruit.Reason = "prepared recruitment regression";
    assert(Await(store.SaveRecruitment(recruit, std::nullopt)));
    auto recruits = Await(store.LoadRecruitments(200));
    assert(recruits->Fetch()[9].GetType() == DatabaseFieldTypes::Int64);
    assert(recruits->Fetch()[9].Get<uint64>() == 0);
    assert(recruits->Fetch()[5].GetType() == DatabaseFieldTypes::Int64);
    assert(recruits->Fetch()[5].Get<uint64>() == 0);
    before = {201, 7, 0, 0, EnrollmentState::Absent, true, true};
    stage = {
        201, 7, 77, 0, EnrollmentRequest::StageBeforeGuildJoin, recruit.StageRequest, "director recruitment guild 77"};
    stage.Director = true;
    stage.ActorGuid = 200;
    assert(Await(enrollment.Commit(stage, before)));
    assert(Await(store.LoadRecruitments(200))->Fetch()[9].Get<uint64>() == 1);
    std::cout << "PASS production GuildStore::Save all nine bindings, PLANNED/CREATING/MANAGED, identity/audit "
                 "rollback, restart\n";
}
GuildIntent StagedIntent(GuildStore& guilds, EnrollmentStore& enrollment, uint32 slot, uint32 founder, uint32 account,
                         std::string const& name)
{
    GuildIntent m;
    m.Slot = slot;
    m.Founder = founder;
    m.Account = account;
    m.Name = name;
    m.Reason = "fixture intent";
    m.StageRequest = GuildRequestId(founder, slot, 0x1001, 0);
    assert(Await(guilds.Save(m, std::nullopt)));
    EnrollmentRecord before{founder, account, 0, 0, EnrollmentState::Absent, true, true};
    TransitionRequest stage{founder,
                            account,
                            0,
                            0,
                            EnrollmentRequest::StageBeforeGuildJoin,
                            m.StageRequest,
                            "director founding slot " + std::to_string(slot)};
    stage.Director = true;
    stage.ActorGuid = founder;
    assert(Await(enrollment.Commit(stage, before)));
    m.StageVerified = true;
    return m;
}
void AddWorldGuild(uint32 id, uint32 founder, std::string const& name)
{
    auto guild = std::make_unique<Guild>();
    guild->Id = id;
    guild->Leader = founder;
    guild->Name = name;
    guild->Members.emplace(founder, Guild::Member{});
    FakeGuildMgr.AddGuild(guild.release());
    FakeCharacterCache.Entries.at(founder).GuildId = id;
    if (FakePlayer.Guid == founder)
        FakePlayer.Guild = id;
}
void PumpGuild(GuildRuntime& runtime, unsigned ticks = 100)
{
    for (unsigned i = 0; i < ticks; ++i)
    {
        Runtime().Update(100);
        runtime.Update(100);
    }
}
void TestDelayedCreate(GuildStore& store, EnrollmentStore& enrollment)
{
    Runtime().Stop();
    store.Raw("DELETE FROM playerbot_autonomy_guild_audit");
    store.Raw("DELETE FROM playerbot_autonomy_guild");
    store.Raw("DELETE FROM playerbot_autonomy_recruitment_audit");
    store.Raw("DELETE FROM playerbot_autonomy_recruitment");
    auto m = StagedIntent(store, enrollment, 1, 1, 7, "Ashen Vanguard");
    FakePlayer.Guid = 1;
    FakePlayer.Guild = 0;
    FakePlayer.Online = true;
    FakeCharacterCache.Entries[1] = {7, 0, 1, {}, "Junglatu", 0};
    Runtime().Configure(true);
    Runtime().Start("disposable");
    Pump();
    EnrollmentStore core;
    assert(core.Initialize("disposable"));
    FakeGuildWrite = [&](uint32 id, uint32 founder, std::string const&)
    {
        core.Raw("START TRANSACTION");
        core.Raw("INSERT INTO guild VALUES(" + std::to_string(id) + "," + std::to_string(founder) +
                 ",'Ashen Vanguard')");
    };
    GuildRuntime live;
    GuildSettings settings;
    settings.Enabled = true;
    settings.DryRun = false;
    settings.Target = 1;
    settings.AlliancePercent = 100;
    settings.CooldownMs = 1000;
    live.Configure(settings);
    live.Start("disposable");
    for (unsigned i = 0; i < 200 && !FakeGuildCreates; ++i)
        PumpGuild(live, 1);
    if (!FakeGuildCreates)
        std::cerr << live.Audit(0) << " reserved=" << Runtime().Cache().Reserved()
                  << " identity=" << Runtime().Cache().Get(1).IdentityValid << '\n';
    assert(FakeGuildCreates == 1 && Runtime().Cache().Reserved() == 1);
    m.Guild = 23;
    assert(!Await(store.Created(m))->Fetch()[0].Get<uint64>());
    PumpGuild(live, 80);
    assert(FakeGuildCreates == 1 && live.Intents().at(1).Phase == GuildPhase::Creating);
    core.Raw("COMMIT");
    core.Raw("START TRANSACTION");
    core.Raw("INSERT INTO guild_member VALUES(1,23)");
    assert(!Await(store.Created(m))->Fetch()[0].Get<uint64>());  // guild committed, member not yet
    PumpGuild(live, 80);
    assert(FakeGuildCreates == 1 && Runtime().Cache().Reserved() == 1 && Runtime().Cache().ShouldBlock(1));
    core.Raw("COMMIT");
    assert(Await(store.Created(m))->Fetch()[0].Get<uint64>() == 1);
    assert(live.Audit(0).find("failures=0") != std::string::npos);
    store.Raw(
        "CREATE TRIGGER autonomy_test_managed_fail BEFORE INSERT ON playerbot_autonomy_guild_audit "
        "FOR EACH ROW SET NEW.mutation_applied=IF(NEW.phase='MANAGED',0,NEW.mutation_applied)");
    PumpGuild(live, 40);
    assert(live.Intents().at(1).Phase == GuildPhase::Creating && FakeGuildCreates == 1);
    assert(Runtime().Cache().Reserved() == 1 && Runtime().Cache().ShouldBlock(1));
    store.Raw("DROP TRIGGER autonomy_test_managed_fail");
    PumpGuild(live, 150);
    assert(live.Intents().at(1).Phase == GuildPhase::Managed && FakeGuildCreates == 1);
    assert(Runtime().Cache().Get(1).State == EnrollmentState::Persistent && !Runtime().Cache().Reserved());
    live.Stop();
    FakeGuildWrite = {};
    std::cout << "PASS actual GuildRuntime Create exactly once; independent delayed guild/member commits; no CAS lag "
                 "errors\n";
}
void TestLoginChurn(EnrollmentStore& store)
{
    Runtime().Stop();
    for (uint32 guid = 301; guid <= 8250; ++guid)
        store.Raw("INSERT INTO characters(guid,account) VALUES(" + std::to_string(guid) + ",7)");
    // Valid fake world/account snapshots for 8,250 public fixture characters.
    FakePlayer.Online = false;
    FakeCharacterCache.Account = 7;
    FakeCharacterCache.Guild = 0;
    FakeCharacterCache.Entries[200] = {7, 77, 1, {}, "Fixture", 0};
    Runtime().Configure(true);
    Runtime().Start("disposable");
    auto const before = Runtime().Counters();
    unsigned readyWindows = 0;
    // Concurrent map-thread login invalidations and fail-closed maintenance
    // lookups. Every scheduler tick injects churn, including low GUIDs that
    // used to rewind the entire scan. Join fixes ordering for reproducibility.
    for (unsigned tick = 0; tick < 500; ++tick)
    {
        std::thread logins(
            [tick]
            {
                for (uint32 n = 0; n < 4; ++n)
                {
                    uint32 guid = 300 + (tick * 4 + n) % 7900;
                    Runtime().Invalidate(guid);
                    assert(Runtime().Cache().ShouldBlock(guid));
                }
                Runtime().Invalidate(3);
            });
        logins.join();
        Runtime().Update(50);
        if (Runtime().ActivityReady())
            ++readyWindows;
    }
    auto const loaded = Runtime().Counters();
    assert(Runtime().Cache().Ready() && readyWindows > 100);
    assert(loaded.Pages - before.Pages == 65 && loaded.Refreshes > before.Refreshes);
    assert(Runtime().Cache().Get(8250).IdentityValid);
    assert(Runtime().Cache().Get(1).State == EnrollmentState::Persistent && Runtime().Cache().ShouldBlock(1));
    assert(Runtime().Cache().Get(14).Revision == 2 && Runtime().Cache().ShouldBlock(14));
    // Dirty work cannot starve a periodic sweep either.
    Runtime().Update(60000);
    for (unsigned tick = 0; tick < 400; ++tick)
    {
        Runtime().Invalidate(3);
        Runtime().Update(50);
    }
    assert(Runtime().Counters().Pages >= loaded.Pages + 65 && Runtime().Cache().Ready());
    assert(Runtime().Cache().ShouldBlock(3));
    Runtime().Stop();
    std::cout << "PASS 8250-character binary SQL scan under concurrent login invalidations: initial_pages="
              << loaded.Pages - before.Pages << " activity_ready_windows=" << readyWindows
              << " point_refreshes=" << loaded.Refreshes - before.Refreshes << "; periodic sweep progressed\n";
}
void TestRecovery(GuildStore& store, EnrollmentStore& enrollment)
{
    Runtime().Stop();
    store.Raw("DELETE FROM playerbot_autonomy_guild_audit");
    store.Raw("DELETE FROM playerbot_autonomy_guild");
    enrollment.Raw("DELETE FROM playerbot_autonomy_enrollment_audit WHERE character_guid IN (1,14)");
    enrollment.Raw("DELETE FROM playerbot_autonomy_transition_receipt WHERE request_id LIKE '00000001-%'");
    enrollment.Raw("DELETE FROM playerbot_autonomy_enrollment WHERE character_guid IN (1,14)");
    enrollment.Raw("DELETE FROM guild_member WHERE guid IN (1,14)");
    enrollment.Raw("DELETE FROM guild WHERE guildid IN (23,24)");
    enrollment.Raw("UPDATE characters SET account=1,name='Junglatu',race=8 WHERE guid=1");
    enrollment.Raw("UPDATE characters SET account=2,name='Crudi',race=2 WHERE guid=14");
    auto m = StagedIntent(store, enrollment, 1, 1, 1, "Ashen Vanguard");
    auto good = StagedIntent(store, enrollment, 2, 14, 2, "Amber Covenant");
    // Both fixtures are Horde; faction is immutable once intent has been saved.
    store.Raw("UPDATE playerbot_autonomy_guild SET faction=1 WHERE slot IN (1,2)");
    m.Faction = good.Faction = 1;
    m.Phase = good.Phase = GuildPhase::Creating;
    assert(Await(store.Save(m, GuildPhase::Planned)) && Await(store.Save(good, GuildPhase::Planned)));
    enrollment.Raw("INSERT INTO guild VALUES(23,1,'Ashen Vanguard'),(24,14,'Amber Covenant')");
    enrollment.Raw("INSERT INTO guild_member VALUES(1,23),(14,24)");
    FakePlayer.Guid = 1;
    FakePlayer.Session.Account = 1;
    FakePlayer.Guild = 23;
    FakeCharacterCache.Account = 7;
    FakeCharacterCache.Guild = 0;
    FakeCharacterCache.Entries[1] = {1, 23, 1, {}, "Junglatu", 1};
    FakeCharacterCache.Entries[14] = {2, 24, 1, {}, "Crudi", 1};
    sPlayerbotAIConfig.Accounts = {1, 2, 7};
    AddWorldGuild(23, 1, m.Name);
    AddWorldGuild(24, 14, good.Name);
    good.Guild = 24;
    good.Phase = GuildPhase::Managed;
    assert(Await(store.Save(good, GuildPhase::Creating)));
    EnrollmentRecord before{14, 2, 24, 1, EnrollmentState::Pending, true, true};
    TransitionRequest confirm{14,
                              2,
                              24,
                              1,
                              EnrollmentRequest::ConfirmAfterGuildJoin,
                              GuildRequestId(14, 2, 0x1002, 1),
                              "director founding slot 2"};
    confirm.Director = true;
    confirm.ActorGuid = 14;
    assert(Await(enrollment.Commit(confirm, before)));
    auto const untouched = store
                               .Inspect(
                                   "SELECT CONCAT(phase,':',guild_id,':',changed_at,':',reason) "
                                   "FROM playerbot_autonomy_guild WHERE slot=2")
                               ->Fetch()[0]
                               .Get<std::string>();
    Runtime().Configure(true);
    Runtime().Start("disposable");
    Pump();
    // A fresh adapter after core Create sees CREATING but never invokes the API.
    GuildRuntime restarted;
    GuildSettings settings;
    settings.Enabled = true;
    settings.DryRun = false;
    settings.Target = 2;
    settings.CooldownMs = 1000;
    restarted.Configure(settings);
    restarted.Start("disposable");
    auto const creates = FakeGuildCreates;
    PumpGuild(restarted, 150);
    assert(restarted.Intents().at(1).Phase == GuildPhase::Quarantined && FakeGuildCreates == creates);
    restarted.Stop();
    m.Phase = GuildPhase::Quarantined;
    m.Guild = 23;
    m.Reason = "operator recovery fixture approval";
    for (unsigned mismatch = 0; mismatch < 8; ++mismatch)
    {
        auto wrong = m;
        std::string founderName = "Junglatu";
        if (mismatch == 0)
            wrong.Guild = 24;
        if (mismatch == 1)
            wrong.Account = 2;
        if (mismatch == 2)
            wrong.Founder = 14;
        if (mismatch == 3)
            wrong.StageRevision = 1;
        if (mismatch == 4)
            wrong.StageRequest.back() = '9';
        if (mismatch == 5)
            wrong.Name = "ashen Vanguard";
        if (mismatch == 6)
            founderName = "junglatu";
        if (mismatch == 7)
            wrong.Phase = GuildPhase::Planned;
        assert(!Await(store.Recover(wrong, founderName)));
        assert(Scalar(store,
                      "SELECT COUNT(*) FROM playerbot_autonomy_guild WHERE slot=1 AND phase='QUARANTINED' AND guild_id "
                      "IS NULL") == 1);
    }
    for (auto const& fault : std::vector<std::pair<std::string, std::string>>{
             {"UPDATE characters SET race=1 WHERE guid=1", "UPDATE characters SET race=8 WHERE guid=1"},
             {"UPDATE guild SET leaderguid=14 WHERE guildid=23", "UPDATE guild SET leaderguid=1 WHERE guildid=23"},
             {"UPDATE guild SET name='ashen Vanguard' WHERE guildid=23",
              "UPDATE guild SET name='Ashen Vanguard' WHERE guildid=23"},
             {"UPDATE playerbot_autonomy_enrollment SET revision=3 WHERE character_guid=1",
              "UPDATE playerbot_autonomy_enrollment SET revision=1 WHERE character_guid=1"},
             {"UPDATE playerbot_autonomy_enrollment_audit SET actor_kind='AUTHORIZED_ADMIN' WHERE character_guid=1",
              "UPDATE playerbot_autonomy_enrollment_audit SET actor_kind='RECRUITMENT' WHERE character_guid=1"},
             {"UPDATE playerbot_autonomy_transition_receipt SET expected_revision=9 WHERE request_id='" +
                  m.StageRequest + "'",
              "UPDATE playerbot_autonomy_transition_receipt SET expected_revision=0 WHERE request_id='" +
                  m.StageRequest + "'"},
             {"INSERT INTO guild_member VALUES(2,23)", "DELETE FROM guild_member WHERE guid=2"}})
    {
        store.Raw(fault.first);
        assert(!Await(store.Recover(m, "Junglatu")));
        store.Raw(fault.second);
    }
    // Successful UPDATE followed by duplicate audit must roll the whole write back.
    store.Raw(
        "CREATE TRIGGER autonomy_test_audit_fail BEFORE INSERT ON playerbot_autonomy_guild_audit "
        "FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected audit failure'");
    assert(!Await(store.Recover(m, "Junglatu")));
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild WHERE slot=1 AND phase='QUARANTINED'") == 1);
    store.Raw("DROP TRIGGER autonomy_test_audit_fail");
    auto& runtime = Guilds();
    settings.DryRun = true;
    runtime.Configure(settings);
    runtime.Start("disposable");
    PumpGuild(runtime);
    AutonomyGuildCommands commands;
    auto handler = commands.GetCommands().at(0).Handler;
    ChatHandler console;
    auto const approval = "APPROVE:1:23:1:1:" + m.StageRequest;
    auto const command = "recover 1 23 Junglatu " + approval;
    auto const audits = Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit");
    assert(handler(&console, "recovery-diagnose 1 23"));
    PumpGuild(runtime);
    assert(handler(&console, "recovery-diagnose 1 23"));
    assert(console.Messages.back().find("world_proof=verified") != std::string::npos);
    assert(console.Messages.back().find("durable_membership=observed") != std::string::npos);
    PumpGuild(runtime);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == audits);
    assert(handler(&console, command.c_str()));  // write flag off
    assert(console.Messages.back().find("disabled") != std::string::npos);
    runtime.ConfigureRecovery(true);
    FakeConfig.Values["PlayerbotAutonomy.AllowGuildRecoveryCommands"] = 1;
    assert(runtime.Recover(1, 23, "Junglatu", "APPROVE").find("approval_required") != std::string::npos);
    FakePlayer.Selfbot = true;
    assert(runtime.Recover(1, 23, "Junglatu", approval).find("nothing_queued") != std::string::npos);
    FakePlayer.Selfbot = false;
    sRandomPlayerbotMgr.ExcludedAccounts.insert(1);
    assert(runtime.Recover(1, 23, "Junglatu", approval).find("nothing_queued") != std::string::npos);
    sRandomPlayerbotMgr.ExcludedAccounts.clear();
    FakeGuildMgr.GetGuildById(23)->Members.emplace(2, Guild::Member{});
    assert(runtime.Recover(1, 23, "Junglatu", approval).find("nothing_queued") != std::string::npos);
    FakeGuildMgr.GetGuildById(23)->Members.erase(2);
    FakeConfig.Values["Ra.MinLevel"] = 2;
    assert(handler(&console, command.c_str()));
    assert(console.Messages.back().find("administrator-only") != std::string::npos);
    FakeConfig.Values["Ra.MinLevel"] = 3;
    console.Session = &FakePlayer.Session;
    assert(!handler(&console, command.c_str()));
    console.Session = nullptr;
    assert(handler(&console, command.c_str()));
    runtime.ConfigureRecovery(false);  // revoke approval while asynchronous proof is outstanding
    PumpGuild(runtime);
    assert(runtime.Intents().at(1).Phase == GuildPhase::Quarantined);
    runtime.ConfigureRecovery(true);
    FakeLostCommitAcknowledgement = true;
    assert(handler(&console, command.c_str()));
    PumpGuild(runtime, 1);  // recovery commits, but the callback receives a lost acknowledgement
    FakeLostCommitAcknowledgement = false;
    PumpGuild(runtime);
    assert(runtime.Intents().at(1).Phase == GuildPhase::Managed && runtime.Intents().at(1).Guild == 23);
    assert(Runtime().Cache().Get(1).State == EnrollmentState::Pending && Runtime().Cache().Get(1).Revision == 1);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == audits + 1);
    assert(handler(&console, command.c_str()));
    assert(console.Messages.back().find("already_managed") != std::string::npos);
    settings.DryRun = false;
    runtime.Configure(settings);
    PumpGuild(runtime);
    assert(Runtime().Cache().Get(1).State == EnrollmentState::Persistent && Runtime().Cache().Get(1).Revision == 2);
    assert(Runtime().Cache().Get(14).State == EnrollmentState::Persistent && Runtime().Cache().Get(14).Revision == 2);
    assert(FakeGuildCreates == creates);
    assert(store
               .Inspect("SELECT CONCAT(phase,':',guild_id,':',changed_at,':',reason) "
                        "FROM playerbot_autonomy_guild WHERE slot=2")
               ->Fetch()[0]
               .Get<std::string>() == untouched);
    assert(Scalar(store, "SELECT COUNT(*) FROM guild WHERE guildid IN (23,24)") == 2);
    // Revisit QUARANTINED after recovery without erasing the original receipt.
    FakeGuildMgr.GetGuildById(23)->Leader = 14;
    PumpGuild(runtime);
    assert(runtime.Intents().at(1).Phase == GuildPhase::Quarantined);
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit WHERE slot=1 AND phase='QUARANTINED'") ==
           2);
    assert(Runtime().Cache().ShouldBlock(1) && Runtime().Cache().Get(1).Revision == 2);
    runtime.Stop();
    Runtime().Stop();
    std::cout << "PASS actual restart/quarantine; explicit recovery/command gates, exact identity and STAGE proof; "
                 "atomic rollback, idempotency, normal CONFIRM 1->2; successful guild 24 unchanged\n";
}
int main()
{
    std::cout << std::unitbuf;
    EnrollmentStore enrollment;
    GuildStore guilds;
    assert(enrollment.Initialize("disposable") && guilds.Initialize("disposable"));
    TestEnrollment(enrollment);
    TestGuild(guilds, enrollment);
    TestDelayedCreate(guilds, enrollment);
    TestRecovery(guilds, enrollment);
    TestLoginChurn(enrollment);
    assert(Scalar(enrollment, "SELECT COUNT(*) FROM characters WHERE level<>41 OR money<>12345") == 0);
    std::cout << "PASS character progression unchanged; no worldserver build/link/startup\n";
}
