#include <cassert>
#include <iostream>
#include <thread>

#include "Operations/OperationCoordinator.h"
#include "Persistence/EnrollmentCache.h"
using namespace PlayerbotAutonomy;

RosterMember Member(std::uint32_t guid, CombatRole role, std::uint32_t guild = 23)
{
    RosterMember m;
    m.Guid = guid;
    m.Account = 100 + guid;
    m.Guild = guild;
    m.Faction = 0;
    m.Class = 1;
    m.Level = 40;
    m.Role = role;
    m.EquippedItemLevel = 50;
    m.ObservedAt = 100;
    m.Managed = m.Dedicated = m.Persistent = m.Identity = m.Online = m.Available = m.EquipmentKnown = true;
    m.HumanControlled = false;
    return m;
}
std::vector<RosterMember> Party()
{
    return {Member(1, CombatRole::Tank), Member(2, CombatRole::Healer), Member(3, CombatRole::Dps),
            Member(4, CombatRole::Dps), Member(5, CombatRole::Dps)};
}
int main()
{
    assert(EvidenceRole(true, false, false, true) == CombatRole::Tank);
    assert(EvidenceRole(false, true, false, true) == CombatRole::Healer);
    assert(EvidenceRole(false, false, true, true) == CombatRole::Dps);
    for (unsigned bits = 0; bits < 8; ++bits)
    {
        assert(EvidenceRole(bits & 1, bits & 2, bits & 4, false) == CombatRole::Unknown);
        if (bits != 1 && bits != 2 && bits != 4)
            assert(EvidenceRole(bits & 1, bits & 2, bits & 4, true) == CombatRole::Unknown);
    }
    RosterRules rules;
    auto party = Party();
    assert(!rules.CrossGuild && PlanParty(party, rules, 23, 0, 100).Complete());
    auto original = party[0];
    auto reject = [&](std::string const& reason)
    {
        assert(RosterRejection(party[0], rules, 23, 0, 100) == reason);
        assert(!PlanParty(party, rules, 23, 0, 100).Complete());
        party[0] = original;
    };
    party[0].Managed = false;
    reject("unmanaged_or_human_guild");
    party[0].Dedicated = false;
    reject("human_selfbot_or_unowned_session");
    party[0].HumanControlled = true;
    reject("human_selfbot_or_unowned_session");
    party[0].Persistent = false;
    reject("m1_not_persistent");
    party[0].Class = 10;
    reject("unknown_or_ineligible_class");
    party[0].Identity = false;
    reject("unknown_identity");
    party[0].ObservedAt = 1;
    reject("stale_roster");
    party[0].ObservedAt = 101;
    reject("stale_roster");
    party[0].Faction = 1;
    reject("incompatible_faction");
    party[0].Guild = 24;
    reject("cross_guild_disabled");
    party[0].Reservation = 77;
    reject("reservation_conflict");
    party[0].Online = false;
    reject("offline");
    party[0].Grouped = true;
    reject("group_conflict");
    party[0].Queued = true;
    reject("queue_conflict");
    party[0].InInstance = true;
    reject("instance_conflict");
    party[0].Available = false;
    reject("unavailable");
    party[0].Level = 10;
    reject("level_mismatch");
    party[0].EquipmentKnown = false;
    reject("inadequate_equipment_evidence");
    party[0].Role = CombatRole::Unknown;
    reject("ambiguous_or_unknown_role");
    party[0].Role = static_cast<CombatRole>(255);
    reject("ambiguous_or_unknown_role");
    party[0].Level = 70;
    assert(PlanParty(party, rules, 23, 0, 100).Reason == "level_mismatch");
    party = Party();
    party[1].Role = CombatRole::Dps;
    assert(PlanParty(party, rules, 23, 0, 100).Reason == "missing_healer");
    party = Party();
    party[0].Role = CombatRole::Dps;
    assert(PlanParty(party, rules, 23, 0, 100).Reason == "missing_tank");
    party = Party();
    party[4].Guild = 24;
    assert(!PlanParty(party, rules, 23, 0, 100).Complete());
    rules.CrossGuild = true;
    assert(PlanParty(party, rules, 23, 0, 100).Complete());
    party.push_back(Member(100, CombatRole::Dps));
    auto preferred = PlanParty(party, rules, 23, 0, 100);
    assert(preferred.Complete());
    for (auto const& m : preferred.Members)
        assert(m.Guild == 23);
    // Compatible alternative must win over an earlier incompatible tank.
    party = Party();
    party[0].Level = 70;
    party.push_back(Member(6, CombatRole::Tank));
    assert(PlanParty(party, rules, 23, 0, 100).Leader == 6);
    party = Party();
    party[4].Guid = 3;
    assert(!PlanParty(party, rules, 23, 0, 100).Complete());
    for (unsigned i = 6; i < 8250; ++i)
        party.push_back(Member(i, CombatRole::Dps));
    auto bounded = PlanParty(party, rules, 23, 0, 100);
    assert(bounded.Reason == "candidate_budget_exceeded" && bounded.Considered == 0);

    EnrollmentCache cache;
    cache.SetEnabled(true);
    cache.SetReady(true);
    for (unsigned guid = 1; guid <= 8; ++guid)
        cache.Put({guid, 100 + guid, 23, 2, EnrollmentState::Persistent, true, true});
    assert(cache.AcquireOperation(1, {1, 2, 3, 4, 5}));
    assert(cache.AcquireOperation(1, {1, 2, 3, 4, 5}));  // idempotent
    assert(!cache.AcquireOperation(2, {6, 7, 8, 1}));
    assert(!cache.OperationOwner(6));  // all or nothing
    assert(!cache.Reserve(1));
    for (unsigned guid = 1; guid <= 5; ++guid)
        assert(cache.ShouldBlock(guid) && cache.LoginBlocked(guid));
    cache.ReleaseOperation(2);
    assert(cache.OperationOwner(1) == 1);
    cache.ReleaseOperation(1);
    cache.ReleaseOperation(1);
    assert(!cache.OperationOwner(1) && cache.ShouldBlock(1));  // M1 survives
    assert(cache.Reserve(1));
    assert(!cache.AcquireOperation(2, {1, 2}));
    cache.Release(1);
    bool a = false, b = false;
    std::thread x([&] { a = cache.AcquireOperation(10, {1, 2, 3}); });
    std::thread y([&] { b = cache.AcquireOperation(11, {3, 4, 5}); });
    x.join();
    y.join();
    assert(a != b);
    cache.ReleaseOperation(10);
    cache.ReleaseOperation(11);

    Operation op;
    op.Id = 1;
    op.Guild = 23;
    op.Leader = 1;
    op.Faction = 0;
    op.Members = Party();
    op.CreatedAt = op.ChangedAt = 100;
    op.Deadline = 190;
    op.LeaseUntil = 220;
    for (auto state : {OperationState::Validated, OperationState::Reserved, OperationState::Assembling,
                       OperationState::Active, OperationState::Closing, OperationState::Completed})
    {
        auto next = NextOperation(op, 101, true, true);
        assert(next.State == state && next.Revision == op.Revision + 1 && CanTransition(op.State, next.State));
        op = next;
    }
    assert(SameOperation(op, NextOperation(op, 999, false, false)));
    assert(!CanTransition(OperationState::Completed, OperationState::Proposed));
    assert(!CanTransition(OperationState::Proposed, OperationState::Active));
    for (auto s : {OperationState::Proposed, OperationState::Validated, OperationState::Reserved,
                   OperationState::Assembling, OperationState::Active, OperationState::Closing})
    {
        op.State = s;
        assert(NextOperation(op, 190, true, true).Reason == "operation_timeout");
        assert(NextOperation(op, 101, false, true).State == OperationState::Cancelled);
        assert(NextOperation(op, 101, true, false).State == OperationState::Failed);
        op.State = OperationState::Recovering;
        assert(NextOperation(op, 101, false, false).Reason == "restart_reconciled_no_executor");
    }
    OperationSettings settings;
    assert(!settings.Enabled && !settings.AssembleGroups && settings.Valid());
    settings.AssembleGroups = true;
    assert(!settings.Valid());
    settings.PilotProfile = 1;
    assert(!settings.Valid());
    settings.PilotGuild = 23;
    assert(settings.Valid());
    settings.RealmMaxExpansion = 1;
    assert(settings.Valid());
    settings.RealmMaxExpansion = 2;
    assert(!settings.Valid());
    settings.RealmMaxExpansion = 1;
    settings.PilotProfile = 574;
    assert(!settings.Valid());
    settings.PilotProfile = 1;
    settings.Rules.CrossGuild = true;
    assert(!settings.Valid());
    std::cout
        << "PASS roles, exclusions, deterministic party planning, bounded load, atomic M1 leases, lifecycle/cleanup\n";
}
