#include "Operations/RealGroupExecutor.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace PlayerbotAutonomy;
struct FakeGroups final : GroupPort
{
    GroupEvidence group;
    std::string rejection;
    unsigned creates = 0, adds = 0, disbands = 0, failAdd = 0;
    bool create = true, ungrouped = true, disband = true;
    std::string Admission(Operation const&) const override { return rejection; }
    GroupEvidence Inspect(std::uint32_t id) const override { return id == group.Id ? group : GroupEvidence{}; }
    std::uint32_t Create(std::uint32_t leader) override
    {
        ++creates;
        if (!create)
            return 0;
        group.Id = 17;
        group.Leader = leader;
        group.Members = {leader};
        group.Normal = group.SafeControl = group.SafeToDisband = true;
        ungrouped = false;
        return 17;
    }
    bool Add(Operation const&, RosterMember const& m) override
    {
        if (++adds == failAdd)
            return false;
        group.Members.push_back(m.Guid);
        return true;
    }
    bool Disband(std::uint32_t) override
    {
        ++disbands;
        if (!disband)
            return false;
        group = {};
        ungrouped = true;
        return true;
    }
    bool ParticipantsUngrouped(Operation const&) const override { return ungrouped; }
};
Operation Make()
{
    Operation op;
    op.Id = 42;
    op.Leader = 1;
    op.Guild = 23;
    op.Execution = ExecutionMode::RealGroup;
    op.State = OperationState::Assembling;
    op.CreationFenced = true;
    for (unsigned n = 1; n <= 5; ++n)
    {
        RosterMember m;
        m.Guid = n;
        m.Role = n == 1 ? CombatRole::Tank : n == 2 ? CombatRole::Healer : CombatRole::Dps;
        op.Members.push_back(m);
    }
    return op;
}
int main()
{
    auto op = Make();
    {
        FakeGroups f;
        RealGroupExecutor e(f);
        assert(!e.Assemble(op, false).Ready && !f.creates);
        auto r = e.Assemble(op, true);
        assert(r.Ready && f.creates == 1 && f.adds == 4);
        op.Group = r.Group;
        assert(e.Assemble(op, false).Ready && f.creates == 1);
        assert(e.Cleanup(op).Clean && f.disbands == 1);
        assert(e.Cleanup(op).Clean && f.disbands == 1);
    }
    op = Make();
    for (unsigned fail = 1; fail <= 4; ++fail)
    {
        FakeGroups f;
        f.failAdd = fail;
        RealGroupExecutor e(f);
        auto r = e.Assemble(op, true);
        assert(!r.Ready && r.Group == 17);
        auto partial = op;
        partial.Group = r.Group;
        assert(e.Cleanup(partial).Clean && f.disbands == 1);
    }
    for (auto reason : {"wrong_leader", "missing_healer", "missing_tank", "busy_character", "stale_m1", "human",
                        "selfbot", "existing_group", "binding_reset", "wrong_difficulty"})
    {
        FakeGroups f;
        f.rejection = reason;
        RealGroupExecutor e(f);
        assert(e.Assemble(op, true).Reason == reason && !f.creates);
    }
    for (unsigned fault = 0; fault < 7; ++fault)
    {
        FakeGroups f;
        RealGroupExecutor e(f);
        auto r = e.Assemble(op, true);
        auto owned = op;
        owned.Group = r.Group;
        switch (fault)
        {
        case 0:
            f.group.Leader = 2;
            break;
        case 1:
            f.group.Members[4] = 99;
            break;
        case 2:
            f.group.Members.push_back(99);
            break;
        case 3:
            f.group.SafeControl = false;
            break;
        case 4:
            f.group.Normal = false;
            break;
        case 5:
            f.group.SafeToDisband = false;
            break;
        case 6:
            ++owned.Group;
            break;
        }
        assert(!e.Cleanup(owned).Clean && f.disbands == 0);
    }
    {
        FakeGroups f;
        RealGroupExecutor e(f);
        auto half = op;
        half.Group = 17;
        f.ungrouped = false;
        assert(!e.Cleanup(half).Clean && !e.Assemble(op, false).Ready && !f.creates);
    }
    auto real = Make();
    assert(SameOperation(real, NextOperation(real, 999999, true, true)));
    auto start = std::chrono::steady_clock::now();
    for (unsigned n = 0; n < 10000; ++n)
    {
        FakeGroups f;
        RealGroupExecutor e(f);
        auto o = Make();
        o.Group = e.Assemble(o, true).Group;
        assert(e.Cleanup(o).Clean);
    }
    std::cout << "PASS production group executor: roster, partial compensation, duplicates, handoff/logout, "
                 "recovery fence, collisions; 10000 fake lifecycles_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()
              << " (engine fakes, no core groups or server)\n";
}
