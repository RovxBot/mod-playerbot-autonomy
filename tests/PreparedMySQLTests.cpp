// Production stores/runtime, actual pinned core Field/PreparedResultSet, real
// binary prepared MySQL. Only engine objects and queue scheduling are faked.
#include <cctype>
#include <limits>

#include "CoreFakes.h"
#include "Guild/GuildStore.h"
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
        "INSERT INTO playerbot_autonomy_guild_audit(slot,phase,reason,mutation_applied) "
        "VALUES(3,'CREATING','existing receipt',1)");
    assert(!Await(store.Save(intent, GuildPhase::Planned)));
    assert(Await(store.Load())->Fetch()[5].Get<std::string>() == "PLANNED");
    assert(Scalar(store, "SELECT COUNT(*) FROM playerbot_autonomy_guild_audit") == 2);
    store.Raw("DELETE FROM playerbot_autonomy_guild_audit WHERE phase='CREATING'");
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
int main()
{
    std::cout << std::unitbuf;
    EnrollmentStore enrollment;
    GuildStore guilds;
    assert(enrollment.Initialize("disposable") && guilds.Initialize("disposable"));
    TestEnrollment(enrollment);
    TestGuild(guilds, enrollment);
    assert(Scalar(enrollment, "SELECT COUNT(*) FROM characters WHERE level<>41 OR money<>12345") == 0);
    std::cout << "PASS character progression unchanged; no worldserver build/link/startup\n";
}
