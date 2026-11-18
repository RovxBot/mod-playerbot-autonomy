#include <cassert>
#include <iostream>
#include <memory>

#include "Guild/GuildDirector.h"
using namespace PlayerbotAutonomy;
struct Port final : GuildPort
{
    bool Loaded = true, Safe = true, StageWorks = true;
    unsigned Writes = 0, Stages = 0, Creates = 0, Confirms = 0, Logins = 0;
    uint32_t Reserved = 0;
    std::map<uint32_t, GuildIntent> Ledger;
    std::map<uint32_t, GuildCandidate> Bots;
    std::optional<GuildIntent> Queued;
    bool Ready() const override { return Loaded && !Queued; }
    std::map<uint32_t, GuildIntent> const& Intents() const override { return Ledger; }
    bool Persist(GuildIntent const& next, std::optional<GuildPhase> before) override
    {
        assert(!Queued);
        if (before)
            assert(Ledger.at(next.Slot).Phase == *before);
        else
            assert(!Ledger.count(next.Slot));
        Queued = next;
        ++Writes;
        return true;
    }
    void Flush()
    {
        if (Queued)
        {
            Ledger[Queued->Slot] = *Queued;
            Queued.reset();
        }
    }
    uint32_t NextCandidate(uint32_t cursor) const override
    {
        auto it = Bots.upper_bound(cursor);
        return it == Bots.end() ? 0 : it->first;
    }
    GuildCandidate Candidate(uint32_t guid) const override { return Bots.at(guid); }
    GuildView Inspect(GuildIntent const& m) const override
    {
        return {m.Guild, m.Founder, 1, m.Faction, Safe, Safe ? "verified" : "human_or_addclass_member"};
    }
    bool Reserve(uint32_t guid) override
    {
        if (Reserved && Reserved != guid)
            return false;
        Reserved = guid;
        return true;
    }
    void Release(uint32_t guid) override
    {
        if (Reserved == guid)
            Reserved = 0;
    }
    bool Offline(uint32_t guid) override
    {
        Bots.at(guid).Online = false;
        return true;
    }
    bool Stage(GuildIntent const& m, EnrollmentRecord const&) override
    {
        ++Stages;
        if (StageWorks)
        {
            Ledger.at(m.Slot).StageVerified = true;
            Bots.at(m.Founder).Enrollment.State = EnrollmentState::Pending;
            ++Bots.at(m.Founder).Enrollment.Revision;
        }
        return StageWorks;
    }
    bool Login(uint32_t guid) override
    {
        ++Logins;
        Bots.at(guid).Online = true;
        return true;
    }
    uint32_t Create(GuildIntent const& m) override
    {
        assert(Bots.at(m.Founder).Enrollment.State == EnrollmentState::Pending);
        ++Creates;
        Bots.at(m.Founder).Enrollment.Guild = 100 + m.Slot;
        return 100 + m.Slot;
    }
    bool Confirm(GuildIntent const& m, EnrollmentRecord const&) override
    {
        assert(m.Guild == Bots.at(m.Founder).Enrollment.Guild);
        ++Confirms;
        Bots.at(m.Founder).Enrollment.State = EnrollmentState::Persistent;
        return true;
    }
    void Step(GuildDirector& director, unsigned n = 1)
    {
        for (unsigned i = 0; i < n; ++i)
        {
            Flush();
            director.Update(1000);
        }
    }
    Port()
    {
        for (uint32_t i = 1; i <= 20; ++i)
            Bots[i] = {
                {i, 100 + i, 0, 0, EnrollmentState::Absent, true, true}, i <= 10 ? 0u : 1u, 1, true, false, "eligible"};
    }
};
GuildSettings Enabled()
{
    GuildSettings s;
    s.Enabled = true;
    s.DryRun = false;
    s.CooldownMs = 1000;
    return s;
}
int main()
{
    Port port;
    GuildDirector director(port);
    port.Step(director, 20);
    assert(port.Writes == 0);
    auto settings = Enabled();
    settings.DryRun = true;
    director.Configure(settings);
    port.Step(director, 20);
    assert(port.Writes == 0 && port.Stages == 0 && port.Logins == 0 && port.Creates == 0);
    settings = Enabled();
    director.Configure(settings);
    port.Step(director, 200);
    assert(port.Ledger.size() == 8 && port.Creates == 8 && port.Confirms == 8 && port.Stages == 8);
    unsigned alliance = 0, horde = 0;
    for (auto const& item : port.Ledger)
    {
        assert(item.second.Phase == GuildPhase::Managed);
        (item.second.Faction ? horde : alliance)++;
    }
    assert(alliance == 4 && horde == 4);
    auto writes = port.Writes;
    port.Step(director, 100);
    assert(port.Writes == writes && port.Creates == 8);
    Port growth;
    GuildDirector growing(growth);
    auto small = settings;
    small.Target = 2;
    growing.Configure(small);
    growth.Step(growing, 80);
    assert(growth.Creates == 2);
    growing.Configure(settings);
    growth.Step(growing, 200);
    assert(growth.Creates == 8);
    unsigned balanced = 0;
    for (auto const& item : growth.Ledger)
        balanced += item.second.Faction == 0;
    assert(balanced == 4);  // A two-guild canary growing to eight must remain 4/4.
    // Existing human guild membership never becomes a founder; no adoption API exists.
    auto c = port.Bots.at(20);
    c.Enrollment.Guild = 1;
    assert(FoundingRejection(c, settings, 1) == "already_guilded");
    c.Enrollment.Guild = 0;
    c.Enrollment.AccountIsRandomBot = false;
    assert(FoundingRejection(c, settings, 1) == "not_dedicated_type1_account");
    c.Enrollment.AccountIsRandomBot = true;
    c.Available = false;
    c.Reason = "group_conflict";
    assert(FoundingRejection(c, settings, 1) == "group_conflict");
    c.Available = true;
    assert(FoundingRejection(c, settings, 0) == "incompatible_faction");
    c.Class = 10;
    assert(FoundingRejection(c, settings, 1) == "ineligible_class");
    // A restart after durable CREATING cannot repeat Create or adopt by leader/name.
    Port crash;
    auto controller = std::make_unique<GuildDirector>(crash);
    controller->Configure(settings);
    for (unsigned i = 0; i < 20; ++i)
    {
        crash.Step(*controller);
        crash.Flush();
        if (!crash.Ledger.empty() && crash.Ledger.begin()->second.Phase == GuildPhase::Creating)
            break;
    }
    assert(crash.Ledger.begin()->second.Phase == GuildPhase::Creating && crash.Creates == 0);
    controller = std::make_unique<GuildDirector>(crash);
    controller->Configure(settings);
    crash.Step(*controller);
    crash.Flush();
    assert(crash.Ledger.begin()->second.Phase == GuildPhase::Quarantined && crash.Creates == 0);
    assert(crash.Bots.at(crash.Ledger.begin()->second.Founder).Enrollment.State == EnrollmentState::Pending);
    Port unrelated;
    unrelated.Ledger[1] = {1, 1, 101, 0, 0, GuildPhase::Planned, "Ashen Vanguard", "unrelated"};
    unrelated.Bots[1].Enrollment.State = EnrollmentState::Pending;
    unrelated.Bots[1].Enrollment.Revision = 1;
    GuildDirector noClaim(unrelated);
    noClaim.Configure(settings);
    unrelated.Step(noClaim, 20);
    assert(unrelated.Creates == 0 && unrelated.Logins == 0 && unrelated.Stages == 0);
    // Restart with committed ownership confirms a protected Pending founder exactly once.
    Port recovery;
    recovery.Ledger[1] = {1, 1, 101, 0, 101, GuildPhase::Managed, "Ashen Vanguard", "owned"};
    recovery.Bots[1].Enrollment.Guild = 101;
    recovery.Bots[1].Enrollment.State = EnrollmentState::Pending;
    recovery.Ledger[1].StageVerified = true;
    GuildDirector restart(recovery);
    restart.Configure(settings);
    recovery.Step(restart, 2);
    assert(recovery.Confirms == 1 && recovery.Creates == 0);
    // Human membership revokes governance but does not release enrollment.
    recovery.Safe = false;
    recovery.Step(restart, 3);
    recovery.Flush();
    assert(recovery.Ledger[1].Phase == GuildPhase::Quarantined &&
           recovery.Bots[1].Enrollment.State == EnrollmentState::Persistent);
    Port failure;
    failure.StageWorks = false;
    GuildDirector failed(failure);
    failed.Configure(settings);
    for (unsigned i = 0; i < 200; ++i)
    {
        failure.Step(failed);
        failure.Flush();
        if (failure.Ledger.count(1) && failure.Ledger[1].Phase == GuildPhase::Quarantined)
            break;
    }
    failure.Flush();
    assert(failure.Ledger[1].Phase == GuildPhase::Quarantined && failure.Creates == 0 && !failure.Reserved);
    settings.MaxMembers = 0;
    director.Configure(settings);
    assert(!director.Settings().Enabled);
    assert(GuildRequestId(187, 1, 0x1001, 0) != GuildRequestId(187, 1, 0x1002, 0));
    assert(GuildRequestId(187, 1, 0x1001, 1) != GuildRequestId(187, 1, 0x1001, 0));
    std::cout << "Founding, budgets, dry-run, ownership, duplicates, failure and restart tests passed\n";
}
