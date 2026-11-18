#include "Persistence/EnrollmentRuntime.h"
#if __has_include("RoutineMaintenancePolicy.h")
#include "RoutineMaintenancePolicy.h"
#define TEST_MAINTENANCE_PRESENT 1
#endif
#include <cassert>
#include <iostream>
#include <tuple>
#if defined(__unix__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#endif

#include "CoreFakes.h"
// Compile and exercise the production command handler, including its access gate.
#include "../src/AutonomyModule.cpp"

using namespace PlayerbotAutonomy;
#ifdef TEST_MAINTENANCE_PRESENT
namespace Maintenance = Playerbots::RoutineMaintenance;
#endif
namespace
{
#if defined(__unix__)
// Verify an unsafe enabled process terminates rather than running without the
// maintenance veto. Run the fatal path in a child, never the CI test process.
template <typename Callback>
void AssertFailsClosed(Callback callback)
{
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        struct rlimit noCore = {0, 0};
        setrlimit(RLIMIT_CORE, &noCore);
        callback();
        _exit(0);  // A surviving child means the safety check failed.
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
#endif
#ifdef TEST_MAINTENANCE_PRESENT
bool CompetingProvider(std::uint32_t, Maintenance::Action) { return false; }
#endif
EnrollmentRecord durable{42, 7, 0, 0, EnrollmentState::Absent, true, true};
std::unordered_map<std::string, std::vector<Field>> receipts;
bool openWorks = true;
bool schemaWorks = true;
bool pageWorks = true;
bool commitWorks = true;
unsigned commitAttempts = 0;
PreparedQueryResult Result(std::vector<Field> row)
{
    auto result = std::make_shared<PreparedResultSet>();
    result->Rows.push_back(std::move(row));
    return result;
}
void Pump(unsigned ticks = 20)
{
    for (unsigned i = 0; i < ticks; ++i)
        Runtime().Update(100);
}
TransitionRequest Request(EnrollmentRequest action, uint64 revision, char last)
{
    TransitionRequest request{
        42, 7, FakePlayer.Guild, revision, action, "00000000-0000-0000-0000-000000000000", "authorized test"};
    request.RequestId.back() = last;
    return request;
}
[[maybe_unused]] void Reload()
{
    Runtime().Configure(true);
    Pump();
    assert(Runtime().Cache().Ready());
}
#ifdef TEST_MAINTENANCE_PRESENT
auto Counts()
{
    auto const& c = Runtime().Counters();
    return std::make_tuple(c.Pages, c.Rows, c.Pending, c.Orphans, c.Failures, c.Committed, c.Replayed, c.Rejected);
}
auto Record(EnrollmentRecord const& r)
{
    return std::make_tuple(r.Guid, r.Account, r.Guild, r.Revision, r.State, r.IdentityValid, r.AccountIsRandomBot);
}
EnrollmentDiagnosis ReadOnlyDiagnosis(TransitionRequest const& request, bool stage = false)
{
    auto& runtime = Runtime();
    auto const calls = FakeDatabaseCalls;
    auto const counts = Counts();
    auto const record = Record(runtime.Cache().Get(request.Guid));
    auto const durableBefore = Record(durable);
    auto const version = runtime.Cache().Version();
    auto const enabled = runtime.Cache().Enabled();
    auto const ready = runtime.Cache().Ready();
    auto const receiptCount = receipts.size();
    auto diagnosis = stage ? runtime.DiagnoseStage(request.Guid) : runtime.Diagnose(request);
    assert(FakeDatabaseCalls == calls && Counts() == counts && receipts.size() == receiptCount);
    assert(Record(runtime.Cache().Get(request.Guid)) == record && Record(durable) == durableBefore);
    assert(runtime.Cache().Version() == version);
    assert(runtime.Cache().Enabled() == enabled && runtime.Cache().Ready() == ready);
    return diagnosis;
}
void Rejected(TransitionRequest const& request, char const* reason, bool stageComparable = true, bool queues = true)
{
    auto const diagnosis = ReadOnlyDiagnosis(request);
    assert(!diagnosis.Passed() && std::string(diagnosis.Reason()) == reason);
    if (stageComparable)
    {
        auto const stage = ReadOnlyDiagnosis(request, true);
        assert(!stage.Passed() && std::string(stage.Reason()) == reason);
        auto const text = FormatStageDiagnosis(stage);
        assert(text.find(std::string("stage_world_checks=FAIL reason=") + reason) != std::string::npos);
    }
    auto const attempts = commitAttempts;
    auto const before = Record(durable);
    auto const rejected = Runtime().Counters().Rejected;
    assert(Runtime().Submit(request) == queues);
    if (queues)
    {
        Pump(2);  // Consume only the preflight receipt callback and the following idle tick.
        assert(Runtime().Counters().Rejected == rejected + 1);
        assert(FakeLastError.find(std::string("reason=") + reason) != std::string::npos);
    }
    assert(commitAttempts == attempts && Record(durable) == before && receipts.empty());
}
void TestDiagnosis()
{
    auto& runtime = Runtime();
    FakePlayer.Online = false;
    auto const request = Request(EnrollmentRequest::StageBeforeGuildJoin, 0, '9');
    auto stage = ReadOnlyDiagnosis(request, true);
    assert(stage.Passed() && ReadOnlyDiagnosis(request).Passed());
    assert(stage.CacheFound && stage.Cached.IdentityValid && stage.CharacterAccount == 7);
    assert(!stage.ConnectedPlayer && !stage.SessionPresent && stage.CharacterGroupGuid == 0);
    auto const text = FormatStageDiagnosis(stage);
    assert(text.find("autonomy_cache_found=true identity_valid=true cached_account=7 cached_guild=0 "
                     "cached_state=ABSENT cached_revision=0") != std::string::npos);
    assert(text.find("random_account=true validation_account=7 character_cache_account=7 "
                     "character_cache_guild=0 character_cache_group_guid=0") != std::string::npos);
    assert(text.find("connected_player=false world_session=not_applicable") != std::string::npos);
    assert(text.find("stage_world_checks=PASS reason=accept expected_revision=0") != std::string::npos);

    std::thread foreign(
        [&]
        {
            auto const result = ReadOnlyDiagnosis(request, true);
            assert(std::string(result.Reason()) == "wrong_world_thread");
            assert(FormatStageDiagnosis(result).find("facts=unobserved") != std::string::npos);
            assert(std::string(ReadOnlyDiagnosis(request).Reason()) == "wrong_world_thread");
        });
    foreign.join();
    runtime.Configure(false);
    Rejected(request, "autonomy_disabled", true, false);
    Reload();
    runtime.Cache().SetReady(false);
    Rejected(request, "cache_not_ready", true, false);
    runtime.Cache().SetReady(true);
    Maintenance::UnregisterProvider(Maintenance::PolicyProvider.load());
    Rejected(request, "maintenance_v1_unavailable", true, false);
    Reload();

    auto bad = request;
    bad.RequestId = "invalid";
    Rejected(bad, "invalid_request_id", false, false);
    bad = request;
    bad.Reason.clear();
    Rejected(bad, "invalid_reason", false);
    bad.Reason.assign(256, 'x');
    Rejected(bad, "invalid_reason", false);
    bad = request;
    bad.ExpectedRevision = std::numeric_limits<uint64>::max();
    Rejected(bad, "revision_overflow", false);
    FakePlayer.Online = true;
    Rejected(request, "stage_requires_offline");
    FakePlayer.HasSession = false;
    Rejected(request, "stage_requires_offline");  // Offline gate still has precedence.
    auto confirm = Request(EnrollmentRequest::ConfirmAfterGuildJoin, 0, '9');
    Rejected(confirm, "connected_player_missing_session", false);
    stage = ReadOnlyDiagnosis(request, true);
    assert(FormatStageDiagnosis(stage).find("connected_player=true world_session=absent") != std::string::npos);
    FakePlayer.HasSession = true;
    stage = ReadOnlyDiagnosis(request, true);
    assert(FormatStageDiagnosis(stage).find("connected_player=true world_session=present") != std::string::npos);
    FakePlayer.Online = false;
    bad = request;
    bad.Guid = 9999;
    Rejected(bad, "autonomy_cache_miss", true, false);
    auto record = runtime.Cache().Get(42);
    record.IdentityValid = false;
    runtime.Cache().Put(record);
    Rejected(request, "cache_identity_invalid", true, false);
    record.IdentityValid = true;
    record.Revision = std::numeric_limits<uint64>::max();
    runtime.Cache().Put(record);
    bad = request;
    bad.ExpectedRevision = record.Revision;
    Rejected(bad, "revision_overflow");
    record.Revision = 0;
    record.Account = 8;
    runtime.Cache().Put(record);
    Rejected(request, "cached_account_mismatch");
    record.Account = 7;
    runtime.Cache().Put(record);
    bad = request;
    bad.ExpectedRevision = 1;
    Rejected(bad, "revision_mismatch", false);
    assert(ReadOnlyDiagnosis(request, true).Passed());  // GUID-only diagnosis uses the current revision.
    FakePlayer.Online = true;
    FakeCharacterCache.Account = 8;
    Rejected(confirm, "character_cache_account_mismatch", false);
    FakeCharacterCache.Account.reset();
    FakePlayer.Online = false;
    record.State = EnrollmentState::Unknown;
    runtime.Cache().Put(record);
    Rejected(request, "unknown_enrollment_state");
    record.State = EnrollmentState::Absent;
    runtime.Cache().Put(record);
    sPlayerbotAIConfig.Accounts.clear();
    Rejected(request, "non_random_account");
    sPlayerbotAIConfig.Accounts.insert(7);
    for (auto state : {EnrollmentState::Pending, EnrollmentState::Persistent})
    {
        record.State = state;
        runtime.Cache().Put(record);
        Rejected(request, "already_in_requested_state");
    }
    record.State = EnrollmentState::Absent;
    runtime.Cache().Put(record);
    FakeCharacterCache.Group = 99;
    Rejected(request, "activity_in_progress");
    stage = ReadOnlyDiagnosis(request, true);
    assert(stage.CharacterGroupGuid == 99 && stage.Facts.IsGrouped);
    FakeCharacterCache.Group.reset();
    for (unsigned boundary = 0; boundary < 4; ++boundary)
    {
        FakePlayer.Online = true;
        FakePlayer.Human = boundary == 0;
        FakePlayer.Selfbot = boundary == 1;
        FakePlayer.AI.HumanMaster = boundary == 2;
        FakePlayer.HasAI = boundary != 3;
        Rejected(confirm, "human_or_selfbot_control", false);
    }
    FakePlayer = Player{};
    FakePlayer.Online = false;
    Rejected(confirm, "no_pending_enrollment", false);
    record.State = EnrollmentState::Pending;
    runtime.Cache().Put(record);
    Rejected(confirm, "guild_join_not_confirmed", false);
    auto cancel = Request(EnrollmentRequest::CancelFailedGuildJoin, 0, '9');
    FakePlayer.Guild = 11;
    Rejected(cancel, "still_guilded", false);
    FakePlayer.Guild = 0;
    FakePlayer.Online = true;
    FakePlayer.OriginalGrouped = true;
    Rejected(cancel, "activity_in_progress", false);
    FakePlayer.OriginalGrouped = false;
    record.State = EnrollmentState::Absent;
    runtime.Cache().Put(record);
    Rejected(Request(EnrollmentRequest::ExplicitRetirement, 0, '9'), "never_enrolled", false);

    // Reproduce Trela's supplied identity without assuming account_type proves
    // membership in the runtime random-account list. No enrollment is attempted.
    FakePlayer.Guid = 187;
    FakePlayer.Session.Account = 19;
    FakePlayer.Guild = 1;
    FakePlayer.Online = false;
    runtime.Cache().Put({187, 19, 1, 0, EnrollmentState::Absent, true, false});
    auto trela = request;
    trela.Guid = 187;
    stage = ReadOnlyDiagnosis(trela, true);
    auto const trelaText = FormatStageDiagnosis(stage);
    assert(trelaText.find("cached_account=19 cached_guild=1 cached_state=ABSENT cached_revision=0") !=
           std::string::npos);
    assert(trelaText.find("random_account=false validation_account=19 character_cache_account=19 "
                          "character_cache_guild=1 character_cache_group_guid=0") != std::string::npos);
    assert(trelaText.find("stage_world_checks=FAIL reason=non_random_account") != std::string::npos);
    sPlayerbotAIConfig.Accounts.insert(19);
    assert(ReadOnlyDiagnosis(trela, true).Passed());  // Already guilded adoption remains allowed.

    EnrollmentCommands commands;
    auto const table = commands.GetCommands();
    assert(table.size() == 1 && table[0].Name == "autonomy");
    assert(table[0].Permission == rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE);
    assert(table[0].AllowConsole == Acore::ChatCommands::Console::Yes);
    auto const calls = FakeDatabaseCalls;
    auto const counts = Counts();
    auto const version = runtime.Cache().Version();
    ChatHandler console;
    // Mutations remain disabled; diagnosis still works and is repeatably read-only.
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(table[0].Handler(&console, "diagnose 187"));
        assert(console.Messages.back() == FormatStageDiagnosis(runtime.DiagnoseStage(187)));
    }
    ChatHandler ingame;
    ingame.Session = &FakePlayer.Session;
    assert(!table[0].Handler(&ingame, "diagnose 187") && ingame.Messages.empty());
    FakeConfig.Values["Ra.MinLevel"] = 2;
    assert(table[0].Handler(&console, "diagnose 187"));
    assert(console.Messages.back().find("administrator access") != std::string::npos);
    FakeConfig.Values["Ra.MinLevel"] = 3;
    for (char const* invalid : {"diagnose", "diagnose 0", "diagnose -1", "diagnose +187", "diagnose trela",
                                "diagnose 187x", "diagnose 4294967296", "diagnose 187 extra", "diagnose 18.7"})
        assert(!table[0].Handler(&console, invalid));
    assert(table[0].Handler(&console, "diagnose 9999"));
    assert(console.Messages.back().find("autonomy_cache_found=false") != std::string::npos);
    assert(FakeDatabaseCalls == calls && Counts() == counts && runtime.Cache().Version() == version);
    assert(receipts.empty() && commitAttempts == 0 && durable.State == EnrollmentState::Absent);
    FakeConfig.Values.clear();
    FakePlayer = Player{};
    sPlayerbotAIConfig.Accounts = {7};
    runtime.Cache().Put(durable);
}
#endif

}  // namespace
namespace PlayerbotAutonomy
{
void EnrollmentConnection::DoPrepareStatements() {}
bool EnrollmentStore::Initialize(std::string const&)
{
    ++FakeDatabaseCalls;
    return openWorks;
}
QueryCallback EnrollmentStore::CheckSchema()
{
    ++FakeDatabaseCalls;
    return QueryCallback(schemaWorks ? Result({Field(uint64(1))}) : nullptr);
}
QueryCallback EnrollmentStore::LoadPage(uint32 cursor, uint32)
{
    ++FakeDatabaseCalls;
    if (!pageWorks)
        return QueryCallback(nullptr);
    if (cursor >= durable.Guid)
        return QueryCallback(Result(std::vector<Field>(8)));
    return QueryCallback(
        Result({Field(uint64(durable.Guid)), Field(uint64(durable.Account)), Field(uint64(FakePlayer.Guild)),
                durable.Revision ? Field(uint64(durable.Account)) : Field(), Field(StateName(durable.State)),
                Field(durable.Revision), Field(uint64(0)), Field()}));
}
QueryCallback EnrollmentStore::FindReceipt(std::string const& id)
{
    ++FakeDatabaseCalls;
    auto itr = receipts.find(id);
    return QueryCallback(Result(itr == receipts.end() ? std::vector<Field>(11) : itr->second));
}
TransactionCallback EnrollmentStore::Commit(TransitionRequest const& request, EnrollmentRecord const& before)
{
    ++FakeDatabaseCalls;
    ++commitAttempts;
    return TransactionCallback(
        [request, before]
        {
            if (!commitWorks)
                return false;
            durable.State = StateAfterAcceptedTransition(request.Action);
            durable.Revision = before.Revision + 1;
            receipts[request.RequestId] = {Field(uint64(request.Guid)),
                                           Field(uint64(request.Account)),
                                           Field(EventName(request.Action)),
                                           Field(StateName(before.State)),
                                           Field(StateName(durable.State)),
                                           Field(uint64(request.Guild)),
                                           Field(std::string("AUTHORIZED_ADMIN")),
                                           Field(uint64(0)),
                                           Field(request.Reason),
                                           Field(before.Revision),
                                           Field(durable.Revision)};
            return true;
        });
}
TransactionCallback EnrollmentStore::ReconcileOrphan(uint32)
{
    ++FakeDatabaseCalls;
    return TransactionCallback([] { return true; });
}
}  // namespace PlayerbotAutonomy
int main()
{
#ifndef TEST_MAINTENANCE_PRESENT
    Runtime().Configure(false);
    Runtime().Start("throwaway");
    assert(!Runtime().ProtectionAvailable() && FakeDatabaseCalls == 0);
    auto const diagnosis = Runtime().DiagnoseStage(42);
    assert(std::string(diagnosis.Reason()) == "autonomy_disabled");
    assert(FormatStageDiagnosis(diagnosis).find("random_account=unavailable") != std::string::npos);
    assert(FakeDatabaseCalls == 0);
    assert(!Runtime().Submit(Request(EnrollmentRequest::StageBeforeGuildJoin, 0, '1')));
#if defined(__unix__)
    AssertFailsClosed([] { Runtime().Configure(true); });
#endif
    std::cout << "Missing-maintenance capability fails closed on enable; legacy mode stays off\n";
#else
#if defined(__unix__)
    assert(Maintenance::RegisterProvider(&CompetingProvider));
    AssertFailsClosed([] { Runtime().Configure(true); });
    Maintenance::UnregisterProvider(&CompetingProvider);
#endif
    auto& runtime = Runtime();
    runtime.Configure(false);
    runtime.Start("throwaway");
    assert(FakeDatabaseCalls == 0);
    assert(!Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    assert(!runtime.Submit(Request(EnrollmentRequest::StageBeforeGuildJoin, 0, '1')));
    runtime.Configure(true);
    assert(Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    Pump();
    assert(runtime.Cache().Ready());
    assert(!Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    assert(Maintenance::ShouldBlockRoutineMaintenance(9999, Maintenance::Action::Randomize));
    assert(Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::LegacyGuildAssignment));
    assert(Maintenance::ShouldBlockRoutineMaintenance(0, Maintenance::Action::AccountDeletion));
    TestDiagnosis();
    auto request = Request(EnrollmentRequest::StageBeforeGuildJoin, 0, '1');
    auto const rejectedBefore = runtime.Counters().Rejected;
    std::thread foreignCaller([&] { assert(!runtime.Submit(request)); });
    foreignCaller.join();
    assert(runtime.Counters().Rejected == rejectedBefore);
    // The actual world-thread authorization adapter must reject each human boundary.
    for (unsigned boundary = 0; boundary < 4; ++boundary)
    {
        FakePlayer.Human = boundary == 0;
        FakePlayer.Selfbot = boundary == 1;
        FakePlayer.AI.HumanMaster = boundary == 2;
        if (boundary == 3)
            sPlayerbotAIConfig.Accounts.clear();
        assert(runtime.Submit(request));
        Pump();
        assert(durable.State == EnrollmentState::Absent);
    }
    FakePlayer.Human = FakePlayer.Selfbot = FakePlayer.AI.HumanMaster = false;
    sPlayerbotAIConfig.Accounts.insert(7);
    FakePlayer.Online = false;
    assert(runtime.Submit(request));
    // A read-only diagnosis may pass while the queue is busy. It must neither
    // replace the queued request nor issue/retry any database work.
    assert(ReadOnlyDiagnosis(request, true).Passed());
    runtime.Update(100);  // preflight receipt, then enqueue commit
    assert(Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    Pump();
    assert(durable.State == EnrollmentState::Pending && runtime.Cache().Ready());
    runtime.Stop();
    runtime.Start("throwaway");
    Pump();
    assert(runtime.Cache().Get(42).State == EnrollmentState::Pending);
    assert(Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    FakePlayer.Guild = 11;
    FakePlayer.Online = true;
    Reload();
    for (unsigned boundary = 0; boundary < 4; ++boundary)
    {
        FakePlayer.Human = boundary == 0;
        FakePlayer.Selfbot = boundary == 1;
        FakePlayer.AI.HumanMaster = boundary == 2;
        if (boundary == 3)
            sPlayerbotAIConfig.Accounts.clear();
        assert(runtime.Submit(Request(EnrollmentRequest::ConfirmAfterGuildJoin, 1, '2')));
        Pump();
        assert(durable.State == EnrollmentState::Pending);
    }
    FakePlayer.Human = FakePlayer.Selfbot = FakePlayer.AI.HumanMaster = false;
    sPlayerbotAIConfig.Accounts.insert(7);
    assert(runtime.Submit(Request(EnrollmentRequest::ConfirmAfterGuildJoin, 1, '2')));
    Pump();
    assert(durable.State == EnrollmentState::Persistent);
    FakePlayer.Guild = 0;
    Reload();
    assert(runtime.Cache().ShouldBlock(42));
    // Replaying STAGE after CONFIRM must leave the newer cache state intact.
    assert(runtime.Submit(request));
    Pump();
    assert(runtime.Cache().Get(42).State == EnrollmentState::Persistent);
    auto const replayed = runtime.Counters().Replayed;
    request.Reason = "different identity";
    assert(runtime.Submit(request));
    Pump();
    assert(runtime.Counters().Replayed == replayed);
    assert(durable.State == EnrollmentState::Persistent);
    FakePlayer.Grouped = true;
    assert(runtime.Submit(Request(EnrollmentRequest::ExplicitRetirement, 2, '3')));
    Pump();
    assert(durable.State == EnrollmentState::Persistent);
    FakePlayer.Grouped = false;
    commitWorks = false;
    assert(runtime.Submit(Request(EnrollmentRequest::ExplicitRetirement, 2, '3')));
    Pump();
    assert(durable.State == EnrollmentState::Persistent && runtime.Cache().ShouldBlock(42));
    commitWorks = true;
    Pump(100);
    assert(runtime.Submit(Request(EnrollmentRequest::ExplicitRetirement, 2, '3')));
    Pump();
    assert(durable.State == EnrollmentState::Retired && !runtime.Cache().ShouldBlock(42));
    pageWorks = false;
    runtime.Configure(true);
    Pump();
    assert(!runtime.Cache().Ready() && runtime.Cache().ShouldBlock(42));
    pageWorks = true;
    Pump(100);
    assert(runtime.Cache().Ready());
    runtime.Configure(true);
    FakeDatabaseReady = false;
    Pump(400);
    assert(!runtime.Cache().Ready());
    assert(!runtime.Submit(Request(EnrollmentRequest::StageBeforeGuildJoin, 3, '4')));
    runtime.Configure(false);
    assert(!Maintenance::ShouldBlockRoutineMaintenance(42, Maintenance::Action::Randomize));
    FakeDatabaseReady = true;
    Pump();
    Reload();
    unsigned const calls = FakeDatabaseCalls;
    for (unsigned i = 0; i < 10000; ++i)
        runtime.Cache().ShouldBlock(42);
    assert(FakeDatabaseCalls == calls);
    runtime.Configure(false);
    runtime.Stop();
    openWorks = false;
    runtime.Configure(true);
    runtime.Start("throwaway");
    assert(!runtime.Cache().Ready() && runtime.Cache().ShouldBlock(42));
    runtime.Configure(false);
    assert(!runtime.Cache().ShouldBlock(42));
    runtime.Stop();
    openWorks = true;
    schemaWorks = false;
    runtime.Configure(true);
    runtime.Start("throwaway");
    Pump();
    assert(!runtime.Cache().Ready());
    schemaWorks = true;
    Pump(100);
    assert(runtime.Cache().Ready());
    runtime.Configure(false);
    runtime.Stop();
    std::cout << "Runtime tests passed with deterministic engine/DB boundary fakes; no game server executed\n";
#endif
}
