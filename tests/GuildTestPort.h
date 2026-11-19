#ifndef AUTONOMY_GUILD_TEST_PORT_H
#define AUTONOMY_GUILD_TEST_PORT_H
#include <cassert>
#include <thread>

#include "Guild/GuildDirector.h"
using namespace PlayerbotAutonomy;
struct Port : GuildPort
{
    bool Loaded = true, Safe = true, StageWorks = true, DurableGuild = true, SaveWorks = true, CreateWorks = true;
    unsigned Writes = 0, Stages = 0, Creates = 0, Confirms = 0, Logins = 0;
    uint32_t Reserved = 0;
    std::map<uint32_t, GuildIntent> Ledger;
    std::map<uint32_t, GuildCandidate> Bots;
    std::optional<GuildIntent> Queued;
    std::thread::id Owner = std::this_thread::get_id();
    bool OnWorldThread() const override { return std::this_thread::get_id() == Owner; }
    bool Ready() const override { return Loaded && !Queued; }
    GuildNameChoice ChooseName() const override
    {
        return SelectGuildName(GuildNamePool,[this](std::string const& name)
        {
            for (auto const& item : Ledger)
                if (item.second.Name == name) return std::string("persisted_founding_intent");
            return std::string{};
        });
    }
    std::map<uint32_t, GuildIntent> const& Intents() const override { return Ledger; }
    bool Persist(GuildIntent const& next, std::optional<GuildPhase> before) override
    {
        if (!SaveWorks)
            return false;
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
        if (!CreateWorks)
            return 0;
        Bots.at(m.Founder).Enrollment.Guild = 100 + m.Slot;
        return 100 + m.Slot;
    }
    bool Durable(GuildIntent const&) override { return DurableGuild; }
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

#endif
