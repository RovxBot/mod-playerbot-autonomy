#include "Persistence/EnrollmentRuntime.h"
#if __has_include("RoutineMaintenancePolicy.h")
#include "RoutineMaintenancePolicy.h"
#define TEST_MAINTENANCE_PRESENT 1
#endif
#include <cassert>
#include <iostream>

#include "CoreFakes.h"

using namespace PlayerbotAutonomy;
#ifdef TEST_MAINTENANCE_PRESENT
namespace Maintenance = Playerbots::RoutineMaintenance;
#endif
namespace
{
EnrollmentRecord durable{42, 7, 0, 0, EnrollmentState::Absent, true, true};
std::unordered_map<std::string, std::vector<Field>> receipts;
bool openWorks = true;
bool schemaWorks = true;
bool pageWorks = true;
bool commitWorks = true;
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
    Runtime().Configure(true);
    Runtime().Start("throwaway");
    Pump();
    assert(!Runtime().ProtectionAvailable() && FakeDatabaseCalls == 0);
    assert(!Runtime().Submit(Request(EnrollmentRequest::StageBeforeGuildJoin, 0, '1')));
    std::cout << "Missing-maintenance capability test passed; enrollment unavailable\n";
#else
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
