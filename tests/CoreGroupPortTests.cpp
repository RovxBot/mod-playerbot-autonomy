#include "GroupEngineFakes.h"
#include "Operations/RealGroupExecutor.h"
#include <cassert>
#include <iostream>
using namespace PlayerbotAutonomy;
Operation Setup()
{
    Players.clear();
    Cache.Entries.clear();
    Groups.Groups.clear();
    Operation op;
    op.Id = 42;
    op.Leader = 1;
    op.Guild = 23;
    op.Faction = 0;
    op.State = OperationState::Assembling;
    op.Execution = ExecutionMode::RealGroup;
    op.CreationFenced = true;
    for (uint32 n = 1; n <= 5; ++n)
    {
        Player p;
        p.Id = n;
        Players[n] = p;
        Cache.Entries[n] = {};
        RosterMember m;
        m.Guid = n;
        m.Account = 7;
        m.Guild = 23;
        m.Role = n == 1 ? CombatRole::Tank : n == 2 ? CombatRole::Healer : CombatRole::Dps;
        op.Members.push_back(m);
    }
    return op;
}
int main()
{
    RegisterCoreGroupObserver();
    auto& port = CoreGroups();
    for (unsigned fault = 0; fault < 18; ++fault)
    {
        auto op = Setup();
        auto& p = Players.at(3);
        auto& row = Runtime().Cache().Row;
        switch (fault)
        {
        case 0:
            p.Human = true;
            break;
        case 1:
            p.Self = true;
            break;
        case 2:
            p.AI.HumanMaster = true;
            break;
        case 3:
            p.Bound = true;
            break;
        case 4:
            p.Combat = true;
            break;
        case 5:
            p.Queue = true;
            break;
        case 6:
            p.Teleport = true;
            break;
        case 7:
            p.DungeonDifficulty = DUNGEON_DIFFICULTY_HEROIC;
            break;
        case 8:
            p.Team = 1;
            break;
        case 9:
            p.Level = 69;
            break;
        case 10:
            p.AI.Gear = 114;
            break;
        case 11:
            row.IdentityValid = false;
            break;
        case 12:
            row.State = EnrollmentState::Unknown;
            break;
        case 13:
            Sessions.Human = true;
            break;
        case 14:
            Random.Dedicated = false;
            break;
        case 15:
            Cloud.Cluster = true;
            break;
        case 16:
            Maps.Allowed = false;
            break;
        case 17:
            op.Members[1].Role = CombatRole::Dps;
            break;
        }
        auto calls = CreateCalls;
        assert(!RealGroupExecutor(port).Assemble(op, true).Ready && CreateCalls == calls);
        row = {};
        Sessions.Human = false;
        Random.Dedicated = true;
        Cloud.Cluster = false;
        Maps.Allowed = true;
    }
    auto op = Setup();
    auto result = RealGroupExecutor(port).Assemble(op, true);
    assert(result.Ready && CreateCalls == 1 && AddCalls == 5);
    op.Group = result.Group;
    auto* actual = Groups.GetGroupByGUID(op.Group);
    assert(actual && actual->Leader == 1 && actual->Slots.size() == 5);
    Players.at(5).Human = true;
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls);
    Players.at(5).Human = false;
    Players.at(4).Location.Dungeon = true;
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls);
    Players.at(4).Location.Dungeon = false;
    assert(RealGroupExecutor(port).Cleanup(op).Clean && DisbandCalls == 1);
    assert(RealGroupExecutor(port).Cleanup(op).Clean && DisbandCalls == 1);
    // Leadership changes remain a handoff even when someone changes it back
    // before the next scheduler observation.
    auto next = Setup();
    next.Group = RealGroupExecutor(port).Assemble(next, true).Group;
    actual = Groups.GetGroupByGUID(next.Group);
    Observer->OnChangeLeader(actual, {2}, {1});
    Observer->OnChangeLeader(actual, {1}, {2});
    assert(!RealGroupExecutor(port).Cleanup(next).Clean && DisbandCalls == 1);
    actual->Disband();
    assert(RealGroupExecutor(port).Cleanup(next).Clean);
    std::cout
        << "PASS actual CoreGroupPort.cpp with fake engine APIs: live M1/control/role/level/gear/access/binding gates, "
           "five members/tank leader, human handoff, instance-safe cleanup; no real core group execution\n";
}
