#include "RecruitmentDirector.h"

#include <algorithm>
namespace PlayerbotAutonomy
{
char const* RecruitmentPhaseName(RecruitmentPhase phase)
{
    switch (phase)
    {
        case RecruitmentPhase::Selected:
            return "SELECTED";
        case RecruitmentPhase::Joining:
            return "JOINING";
        case RecruitmentPhase::Complete:
            return "COMPLETE";
        case RecruitmentPhase::Quarantined:
            return "QUARANTINED";
    }
    return "QUARANTINED";
}
void RecruitmentDirector::Configure(RecruitmentSettings settings, GuildSettings guildSettings)
{
    _settings = settings;
    _guildSettings = guildSettings;
    if (!settings.Valid() || !guildSettings.Valid())
    {
        _settings.Enabled = false;
        _last = "invalid_configuration";
    }
    _settings.DryRun = settings.DryRun || guildSettings.DryRun;
    _settings.Enabled = _settings.Enabled && guildSettings.Enabled;
    _cooldown = settings.CooldownMs;
    _untilStep = 0;
    if (!_settings.Enabled || _settings.DryRun)
    {
        _guilds.Release(_reserved);
        _reserved = 0;
    }
}
GuildIntent const* RecruitmentDirector::Managed(std::uint32_t guild) const
{
    for (auto const& item : _guilds.Intents())
        if (item.second.Guild == guild && item.second.Phase == GuildPhase::Managed)
            return &item.second;
    return nullptr;
}
std::string RecruitmentDirector::Plan(std::uint32_t guid, std::uint32_t guild, std::uint32_t actor) const
{
    auto* managed = Managed(guild);
    if (!managed)
        return "no_explicit_managed_ownership";
    if (_recruits.Recruitments().count(guid))
        return "duplicate_or_prior_recruitment_intent";
    auto view = _guilds.Inspect(*managed);
    if (!view.Safe)
        return view.Reason;
    if (!_recruits.AuthorizedRecruiter(*managed, actor))
        return "actor_not_authorized_bot_leader_or_officer";
    if (view.Members >= _guildSettings.MaxMembers)
        return "membership_cap_reached";
    return FoundingRejection(_guilds.Candidate(guid), _guildSettings, managed->Faction);
}
bool RecruitmentDirector::Request(std::uint32_t guid, std::uint32_t guild, std::uint32_t actor)
{
    if (!_settings.Enabled || _settings.DryRun || !_guilds.Ready())
    {
        _last = "activities_disabled_dry_run_or_snapshot_unready";
        return false;
    }
    if (_cooldown)
    {
        _last = "recruitment_cooldown";
        return false;
    }
    if (_recruits.ActiveRecruitment())
    {
        _last = "recruitment_job_in_progress";
        return false;
    }
    auto reason = Plan(guid, guild, actor);
    if (!reason.empty())
    {
        _last = reason;
        return false;
    }
    auto* managed = Managed(guild);
    auto c = _guilds.Candidate(guid);
    RecruitmentIntent m;
    m.Guid = guid;
    m.Account = c.Enrollment.Account;
    m.Guild = guild;
    m.Slot = managed->Slot;
    m.Actor = actor;
    m.StageRevision = c.Enrollment.Revision;
    m.StageRequest = GuildRequestId(guid, guild, 0x2001, m.StageRevision);
    m.Reason = "director recruitment intent";
    if (!_recruits.PersistRecruitment(m, std::nullopt))
    {
        _last = "recruitment_store_busy";
        return false;
    }
    _cooldown = _settings.CooldownMs;
    _last = "recruitment_intent_queued";
    return true;
}
void RecruitmentDirector::Quarantine(RecruitmentIntent m, std::string reason)
{
    auto before = m.Phase;
    m.Phase = RecruitmentPhase::Quarantined;
    m.Reason = reason;
    if (_recruits.PersistRecruitment(m, before))
    {
        _guilds.Release(_reserved);
        _reserved = 0;
    }
    _last = std::move(reason);
}
void RecruitmentDirector::Update(std::uint32_t diff)
{
    _cooldown = diff >= _cooldown ? 0 : _cooldown - diff;
    if (!_settings.Enabled)
        return;
    if (_active)
        _age = std::min(300000u, _age + std::min(diff, 300000u));
    if (diff < _untilStep)
    {
        _untilStep -= diff;
        return;
    }
    _untilStep = _settings.IntervalMs;
    if (!_guilds.Ready())
    {
        _last = "awaiting_ready_snapshot";
        return;
    }
    if (auto job = _recruits.ActiveRecruitment())
    {
        if (_settings.DryRun)
        {
            _last = "dry_run_pending_recruitment=" + std::to_string(job->Guid);
            return;
        }
        if (_active != job->Guid)
        {
            _active = job->Guid;
            _age = 0;
        }
        Progress(*job);
        return;
    }
    _active = 0;
    if (_cooldown)
    {
        _last = "recruitment_cooldown";
        return;
    }
    auto guild = _guilds.Intents().upper_bound(_guildCursor);
    if (guild == _guilds.Intents().end())
    {
        _guildCursor = 0;
        _last = "managed_roster_cycle_complete";
        return;
    }
    _guildCursor = guild->first;
    auto const& m = guild->second;
    if (m.Phase != GuildPhase::Managed)
    {
        _last = "guild_not_managed";
        return;
    }
    auto view = _guilds.Inspect(m);
    if (!view.Safe || view.Members >= _guildSettings.MaxMembers)
    {
        _last = view.Safe ? "membership_cap_reached" : view.Reason;
        return;
    }
    if (!_recruits.AuthorizedRecruiter(m, m.Founder))
    {
        _last = "actor_not_authorized_bot_leader_or_officer";
        return;
    }
    for (std::uint32_t i = 0; i < _settings.CandidateBudget; ++i)
    {
        auto guid = _guilds.NextCandidate(_cursor);
        if (!guid)
        {
            _cursor = 0;
            _last = "candidate_page_exhausted";
            return;
        }
        _cursor = guid;
        ++_considered;
        auto reason = _recruits.Recruitments().count(guid)
                          ? "duplicate_or_prior_recruitment_intent"
                          : FoundingRejection(_guilds.Candidate(guid), _guildSettings, m.Faction);
        if (!reason.empty())
        {
            _last = reason;
            continue;
        }
        if (_settings.DryRun)
        {
            _last = "dry_run_recruit=" + std::to_string(guid) + " guild=" + std::to_string(m.Guild);
            return;
        }
        Request(guid, m.Guild, m.Founder);
        return;
    }
}
void RecruitmentDirector::Progress(RecruitmentIntent const& m)
{
    if (_age >= 180000)
    {
        Quarantine(m, "recruitment_timeout_pending_retained");
        return;
    }
    auto* managed = Managed(m.Guild);
    if (!managed || managed->Slot != m.Slot)
    {
        Quarantine(m, "managed_guild_missing_or_quarantined");
        return;
    }
    auto view = _guilds.Inspect(*managed);
    if (!view.Safe)
    {
        if (view.Reason == "founder_snapshot_stale")
        {
            _last = view.Reason;
            return;
        }
        Quarantine(m, view.Reason);
        return;
    }
    if (!_recruits.AuthorizedRecruiter(*managed, m.Actor))
    {
        Quarantine(m, "recruiter_authority_changed");
        return;
    }
    auto c = _guilds.Candidate(m.Guid);
    if (!c.Enrollment.IdentityValid)
    {
        _last = "candidate_snapshot_stale";
        return;
    }
    if (!c.Enrollment.AccountIsRandomBot || c.Enrollment.Account != m.Account || c.Faction != managed->Faction)
    {
        Quarantine(m, "candidate_identity_or_faction_changed");
        return;
    }
    if (!c.Class || c.Class > 11 || !(_guildSettings.ClassMask & (1u << (c.Class - 1))))
    {
        Quarantine(m, "class_no_longer_enabled");
        return;
    }
    if (c.Enrollment.Guild && c.Enrollment.Guild != m.Guild)
    {
        Quarantine(m, "candidate_joined_other_guild");
        return;
    }
    if (c.Enrollment.State == EnrollmentState::Persistent)
    {
        if (!m.StageVerified)
        {
            Quarantine(m, "persistent_without_director_stage_receipt");
            return;
        }
        if (c.Enrollment.Guild != m.Guild || !_recruits.IsRecruitMember(m.Guild, m.Guid))
        {
            Quarantine(m, "persistent_membership_changed");
            return;
        }
        auto next = m;
        next.Phase = RecruitmentPhase::Complete;
        next.Reason = "membership_and_persistence_confirmed";
        if (_recruits.PersistRecruitment(next, m.Phase))
        {
            _guilds.Release(_reserved);
            _reserved = 0;
        }
        return;
    }
    if (c.Enrollment.State == EnrollmentState::Absent || c.Enrollment.State == EnrollmentState::Retired)
    {
        if (m.Phase != RecruitmentPhase::Selected || c.Enrollment.Revision != m.StageRevision || c.Enrollment.Guild)
        {
            Quarantine(m, "enrollment_revision_changed");
            return;
        }
        if (!c.Available)
        {
            _last = c.Reason;
            return;
        }
        if (!_reserved)
        {
            if (!_guilds.Reserve(m.Guid))
            {
                _last = "enrollment_reservation_busy";
                return;
            }
            _reserved = m.Guid;
        }
        if (!_guilds.Offline(m.Guid))
        {
            _last = "awaiting_normal_logout_save";
            return;
        }
        _recruits.StageRecruit(m, c.Enrollment);
        return;
    }
    if (c.Enrollment.State != EnrollmentState::Pending || !m.StageVerified ||
        c.Enrollment.Revision != m.StageRevision + 1)
    {
        _last = "awaiting_exact_director_stage_receipt";
        return;
    }
    if (c.Enrollment.Guild == m.Guild && _recruits.IsRecruitMember(m.Guild, m.Guid))
    {
        _recruits.ConfirmRecruit(m, c.Enrollment);
        return;
    }
    if (!c.Available)
    {
        _last = c.Reason;
        return;
    }
    if (view.Members >= _guildSettings.MaxMembers)
    {
        Quarantine(m, "membership_cap_reached_after_stage");
        return;
    }
    if (!_reserved)
    {
        if (!_guilds.Reserve(m.Guid))
        {
            _last = "enrollment_reservation_busy";
            return;
        }
        _reserved = m.Guid;
    }
    if (!_guilds.Offline(m.Guid))
    {
        _last = "awaiting_normal_logout_save";
        return;
    }
    if (m.Phase == RecruitmentPhase::Selected)
    {
        auto next = m;
        next.Phase = RecruitmentPhase::Joining;
        next.Reason = "authorized internal recruitment";
        _recruits.PersistRecruitment(next, m.Phase);
        return;
    }
    if (!_recruits.JoinRecruit(m))
        Quarantine(m, "guild_add_failed_pending_retained");
}
}  // namespace PlayerbotAutonomy
