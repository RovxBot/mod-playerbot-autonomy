#include "GroupEngineFakes.h"
#include "Operations/RealGroupExecutor.h"
#include "Operations/DungeonBridge.h"
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
        p.Level = 17;
        p.AI.Gear = 13;
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
#if !AUTONOMY_DC_BRIDGE
    auto unavailable = Setup();
    assert(!port.Admission(unavailable).empty());
    assert(!RealGroupExecutor(port).Assemble(unavailable, true).Ready && CreateCalls == 0);
    std::cout << "PASS actual core adapter without compatible pilot headers: no group mutation\n";
    return 0;
#else
    assert(!port.Admission(Setup()).empty()); // Explicit profile opt-in is required.
    port.ConfigurePilot(1, 2);
    assert(!port.Admission(Setup()).empty()); // Never accepts a Wrath realm cap.
    port.ConfigurePilot(574, 1);
    assert(!port.Admission(Setup()).empty());
    port.ConfigurePilot(1, 1);
    // A cached/high equipped average cannot admit missing mandatory equipment
    // or any broken equipped item, for any of the five selected characters.
    for (uint32 guid = 1; guid <= 5; ++guid)
        for (uint8 fault = 0; fault <= EQUIPMENT_SLOT_END; ++fault)
        {
            auto invalid = Setup();
            auto& p = Players.at(guid);
            p.AI.Gear = 180;
            if (fault == EQUIPMENT_SLOT_END)
                p.Equipment[EQUIPMENT_SLOT_MAINHAND].Present = false;
            else
                p.Equipment[fault].Broken = true;
            auto creates = CreateCalls, adds = AddCalls;
            auto result = RealGroupExecutor(port).Assemble(invalid, true);
            assert(!result.Ready && !result.Group && result.Reason == "pilot_equipment_missing_or_broken");
            assert(CreateCalls == creates && AddCalls == adds);
        }
    for (unsigned fault = 0; fault < 19; ++fault)
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
            p.Level = 14;
            break;
        case 10:
            p.AI.Gear = 12;
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
        case 18:
            p.Level = 71;
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
    assert(Maps.LastMap == 389);
    op.Group = result.Group;
    auto* actual = Groups.GetGroupByGUID(op.Group);
    assert(actual && actual->Leader == 1 && actual->Slots.size() == 5);
    Players.at(5).Human = true;
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls);
    Players.at(5).Human = false;
    // Failed/partial portal entry and wrong instance identity never become
    // valid colocation or permission to disband an inside/bound character.
    for (auto& member : Players)
    {
        member.second.MapId = 389;
        member.second.InstanceId = 88;
        member.second.Location.Dungeon = true;
        member.second.Bound = true;
    }
    Players.at(5).MapId = 1; // Failed portal entry, separated from the other four.
    assert(port.Inspect(op.Group).Map == 0 && port.Inspect(op.Group).Instance == 0);
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls);
    Players.at(5).MapId = 389;
    Players.at(5).InstanceId = 89;
    assert(port.Inspect(op.Group).Map == 0 && port.Inspect(op.Group).Instance == 0);
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls);
    Players.at(5).InstanceId = 88;
    assert(port.Inspect(op.Group).Map == 389 && port.Inspect(op.Group).Instance == 88);
    for (auto& member : Players)
    {
        member.second.MapId = 1;
        member.second.InstanceId = 0;
        member.second.Location.Dungeon = false;
    }
    assert(!RealGroupExecutor(port).Cleanup(op).Clean && !DisbandCalls); // Exit leaves normal bindings.
    for (auto& member : Players)
        member.second.Bound = false; // Inject lawful reconciled absence, never force-unbind.
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
    // Re-entrant group hooks can change gear after the initial admission.
    // The follower path must apply the same current equipment check again.
    for (bool missing : {false, true})
    {
        auto changed = Setup();
        assert(port.Admission(changed).empty());
        changed.Group = port.Create(changed.Leader);
        assert(changed.Group);
        auto& weapon = Players.at(2).Equipment[EQUIPMENT_SLOT_MAINHAND];
        if (missing)
            weapon.Present = false;
        else
            weapon.Broken = true;
        auto adds = AddCalls;
        assert(!port.Add(changed, changed.Members[1]) && AddCalls == adds);
        assert(RealGroupExecutor(port).Cleanup(changed).Clean);
    }
    std::cout
        << "PASS actual CoreGroupPort.cpp with fake engine APIs: live M1/control/role/level/gear/access/binding gates, "
           "five members/tank leader, human handoff, instance-safe cleanup; no real core group execution\n";
#endif
}
