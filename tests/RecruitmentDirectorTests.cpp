#include <iostream>

#include "Guild/RecruitmentDirector.h"
#include "GuildTestPort.h"
struct Fixture final : Port, RecruitmentPort
{
    std::map<uint32_t, RecruitmentIntent> Jobs;
    std::optional<RecruitmentIntent> Write;
    std::set<uint32_t> Members{1};
    unsigned RecruitWrites = 0, Adds = 0, RecruitStages = 0, RecruitConfirms = 0;
    bool Authority = true, AddWorks = true, ProofWorks = true;
    bool Ready() const override { return Port::Ready() && !Write; }
    GuildView Inspect(GuildIntent const& m) const override
    {
        auto v = Port::Inspect(m);
        v.Members = Members.size();
        return v;
    }
    std::map<uint32_t, RecruitmentIntent> const& Recruitments() const override { return Jobs; }
    std::optional<RecruitmentIntent> ActiveRecruitment() const override
    {
        for (auto const& item : Jobs)
            if (item.second.Phase == RecruitmentPhase::Selected || item.second.Phase == RecruitmentPhase::Joining)
                return item.second;
        return std::nullopt;
    }
    bool PersistRecruitment(RecruitmentIntent const& m, std::optional<RecruitmentPhase> before) override
    {
        assert(!Write);
        if (before)
            assert(Jobs.at(m.Guid).Phase == *before);
        else
            assert(!Jobs.count(m.Guid));
        Write = m;
        ++RecruitWrites;
        return true;
    }
    bool AuthorizedRecruiter(GuildIntent const&, uint32_t actor) const override
    {
        return Authority && Members.count(actor) && (actor == 1 || actor == 90);
    }
    bool IsRecruitMember(uint32_t, uint32_t guid) const override { return Members.count(guid); }
    bool StageRecruit(RecruitmentIntent const& m, EnrollmentRecord const&) override
    {
        ++RecruitStages;
        Bots.at(m.Guid).Enrollment.State = EnrollmentState::Pending;
        ++Bots.at(m.Guid).Enrollment.Revision;
        Jobs.at(m.Guid).StageVerified = ProofWorks;
        return true;
    }
    bool JoinRecruit(RecruitmentIntent const& m) override
    {
        assert(Jobs.at(m.Guid).StageVerified && Bots.at(m.Guid).Enrollment.State == EnrollmentState::Pending);
        if (!AddWorks)
            return false;
        if (Members.insert(m.Guid).second)
            ++Adds;
        Bots.at(m.Guid).Enrollment.Guild = m.Guild;
        return true;
    }
    bool ConfirmRecruit(RecruitmentIntent const& m, EnrollmentRecord const&) override
    {
        assert(Members.count(m.Guid));
        ++RecruitConfirms;
        Bots.at(m.Guid).Enrollment.State = EnrollmentState::Persistent;
        ++Bots.at(m.Guid).Enrollment.Revision;
        return true;
    }
    void FlushAll()
    {
        Flush();
        if (Write)
        {
            Jobs[Write->Guid] = *Write;
            Write.reset();
        }
    }
    void Step(RecruitmentDirector& r, unsigned n = 1)
    {
        for (unsigned i = 0; i < n; ++i)
        {
            FlushAll();
            r.Update(1000);
        }
    }
    Fixture()
    {
        Ledger[1] = {1, 1, 101, 0, 101, GuildPhase::Managed, "Ashen Vanguard", "owned"};
        Ledger[1].StageVerified = true;
        for (uint32_t i = 1; i <= 90; ++i)
            Bots[i] = {{i, 100 + i, i == 1 ? 101u : 0u, i == 1 ? 2u : 0u,
                        i == 1 ? EnrollmentState::Persistent : EnrollmentState::Absent, true, true},
                       0,
                       1,
                       true,
                       false,
                       "eligible"};
    }
};
GuildSettings GuildConfig()
{
    GuildSettings s;
    s.Enabled = true;
    s.DryRun = false;
    s.MaxMembers = 70;
    return s;
}
RecruitmentSettings RecruitConfig()
{
    RecruitmentSettings s;
    s.Enabled = true;
    s.DryRun = false;
    s.CooldownMs = 1000;
    return s;
}
int main()
{
    Fixture f;
    RecruitmentDirector r(f, f);
    auto g = GuildConfig();
    auto s = RecruitConfig();
    r.Configure(s, g);
    f.Step(r, 1500);
    f.FlushAll();
    assert(f.Members.size() == 70 && f.Adds == 69 && f.RecruitStages == 69 && f.RecruitConfirms == 69);
    auto adds = f.Adds, writes = f.RecruitWrites;
    f.Step(r, 50);
    assert(f.Adds == adds && f.RecruitWrites == writes);
    assert(r.Plan(90, 101, 1) == "membership_cap_reached");
    assert(r.Plan(2, 101, 1) == "duplicate_or_prior_recruitment_intent");
    assert(r.Plan(90, 1, 1) == "no_explicit_managed_ownership");
    for (auto guid : f.Members)
        assert(f.Bots[guid].Enrollment.State == EnrollmentState::Persistent);
    auto decision = r.LastDecision();
    std::thread foreign(
        [&]
        {
            r.Update(1000);
            assert(!r.Request(90, 101, 1));
            assert(r.Plan(90, 101, 1) == "wrong_world_thread");
        });
    foreign.join();
    assert(r.LastDecision() == decision && f.Adds == adds && f.RecruitWrites == writes);
    // Default-off and dry run cannot reserve, log out, enroll or change membership.
    Fixture dry;
    RecruitmentDirector d(dry, dry);
    dry.Step(d, 100);
    assert(!dry.RecruitWrites);
    s.DryRun = true;
    d.Configure(s, g);
    dry.Step(d, 100);
    assert(!dry.RecruitWrites && !dry.RecruitStages && !dry.Reserved && !dry.Adds);
    s.DryRun = false;
    Fixture reasons;
    RecruitmentDirector p(reasons, reasons);
    p.Configure(s, g);
    assert(p.Plan(2, 101, 99) == "actor_not_authorized_bot_leader_or_officer");
    reasons.Bots[2].Faction = 1;
    assert(p.Plan(2, 101, 1) == "incompatible_faction");
    reasons.Bots[2].Faction = 0;
    reasons.Bots[2].Enrollment.AccountIsRandomBot = false;
    assert(p.Plan(2, 101, 1) == "not_dedicated_type1_account");
    reasons.Bots[2].Enrollment.AccountIsRandomBot = true;
    reasons.Bots[2].Available = false;
    reasons.Bots[2].Reason = "group_conflict";
    assert(p.Plan(2, 101, 1) == "group_conflict");
    reasons.Bots[2].Available = true;
    reasons.Safe = false;
    assert(p.Plan(2, 101, 1) == "human_or_addclass_member");
    reasons.Safe = true;
    // An officer may request the same audited lifecycle internally, subject to budget.
    reasons.Step(p);
    reasons.FlushAll();
    auto job = reasons.ActiveRecruitment();
    assert(job);
    assert(!p.Request(job->Guid, 101, 2));
    Fixture officer;
    officer.Members.insert(90);
    officer.Bots[90].Enrollment.Guild = 101;
    officer.Bots[90].Enrollment.State = EnrollmentState::Persistent;
    RecruitmentDirector represented(officer, officer);
    auto one = s;
    one.CandidateBudget = 1;
    represented.Configure(one, g);
    officer.Step(represented);  // Founder consumes the single candidate budget.
    assert(represented.Request(2, 101, 90));
    officer.FlushAll();
    assert(officer.Jobs[2].Actor == 90);
    officer.Step(represented, 5);
    officer.FlushAll();
    assert(officer.Bots[2].Enrollment.State == EnrollmentState::Persistent);
    // Restart after guild action before CONFIRM: no second AddMember.
    Fixture crash;
    RecruitmentDirector c(crash, crash);
    c.Configure(s, g);
    for (unsigned i = 0; i < 20 && !crash.Adds; ++i)
        crash.Step(c);
    assert(crash.Adds == 1 && crash.RecruitConfirms == 0);
    auto guid = crash.ActiveRecruitment()->Guid;
    RecruitmentDirector restarted(crash, crash);
    restarted.Configure(s, g);
    crash.Step(restarted, 3);
    crash.FlushAll();
    assert(crash.Adds == 1 && crash.Bots[guid].Enrollment.State == EnrollmentState::Persistent &&
           crash.Jobs[guid].Phase == RecruitmentPhase::Complete);
    // Full guild after STAGE leaves Pending protected and quarantines the request.
    Fixture cap;
    RecruitmentDirector limited(cap, cap);
    g.MaxMembers = 2;
    limited.Configure(s, g);
    while (!cap.RecruitStages)
        cap.Step(limited);
    cap.Members.insert(89);
    cap.Step(limited);
    cap.FlushAll();
    auto id = cap.Jobs.begin()->first;
    assert(cap.Jobs[id].Phase == RecruitmentPhase::Quarantined &&
           cap.Bots[id].Enrollment.State == EnrollmentState::Pending && !cap.Adds);
    // Unrelated Pending proof and lost authority cannot authorize a guild action.
    Fixture proof;
    proof.ProofWorks = false;
    RecruitmentDirector blocked(proof, proof);
    g = GuildConfig();
    blocked.Configure(s, g);
    proof.Step(blocked, 20);
    assert(proof.RecruitStages == 1 && !proof.Adds && !proof.RecruitConfirms);
    proof.Authority = false;
    proof.Step(blocked);
    proof.FlushAll();
    assert(proof.Jobs.begin()->second.Phase == RecruitmentPhase::Quarantined &&
           proof.Bots[proof.Jobs.begin()->first].Enrollment.State == EnrollmentState::Pending);
    // Disable a staged job, restart and continue without manual enrollment.
    Fixture disabled;
    RecruitmentDirector off(disabled, disabled);
    off.Configure(s, g);
    while (!disabled.RecruitStages)
        disabled.Step(off);
    auto active = disabled.ActiveRecruitment()->Guid;
    auto stopped = s;
    stopped.Enabled = false;
    off.Configure(stopped, g);
    disabled.Step(off, 20);
    assert(disabled.Bots[active].Enrollment.State == EnrollmentState::Pending && !disabled.Adds && !disabled.Reserved);
    RecruitmentDirector resumed(disabled, disabled);
    resumed.Configure(s, g);
    disabled.Step(resumed, 6);
    disabled.FlushAll();
    assert(disabled.Bots[active].Enrollment.State == EnrollmentState::Persistent && disabled.Adds == 1);
    std::cout
        << "Recruitment cap 70, budgets, duplicates, human exclusion, officer authority, disable and restart passed\n";
}
