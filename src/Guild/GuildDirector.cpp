#include "GuildDirector.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
namespace PlayerbotAutonomy
{
bool GuildSettings::Valid() const
{
    return Target <= 64 && AlliancePercent <= 100 && MaxMembers >= 1 && MaxMembers <= 200 && CandidateBudget >= 1 &&
           CandidateBudget <= 64 && IntervalMs >= 100 && IntervalMs <= 60000 && CooldownMs >= 1000 &&
           CooldownMs <= 86400000 && ClassMask && !(ClassMask & ~0x5ffu);
}
std::uint32_t NextFoundingFaction(GuildSettings const& settings, std::map<std::uint32_t, GuildIntent> const& intents)
{
    std::uint32_t alliance = 0;
    for (auto const& item : intents)
        if (item.second.Faction == 0)
            ++alliance;
    return alliance < settings.Target * settings.AlliancePercent / 100 ? 0u : 1u;
}
char const* PhaseName(GuildPhase phase)
{
    switch (phase)
    {
        case GuildPhase::Planned:
            return "PLANNED";
        case GuildPhase::Creating:
            return "CREATING";
        case GuildPhase::Managed:
            return "MANAGED";
        case GuildPhase::Quarantined:
            return "QUARANTINED";
    }
    return "QUARANTINED";
}
std::string GuildRequestId(std::uint32_t guid, std::uint32_t scope, std::uint32_t event, std::uint64_t revision)
{
    if (revision > 0xffffffffffffULL || event > 0xffff)
        return {};
    std::ostringstream id;
    id << std::hex << std::setfill('0') << std::setw(8) << guid << '-' << std::setw(4) << (scope >> 16) << '-'
       << std::setw(4) << (scope & 0xffff) << '-' << std::setw(4) << event << '-' << std::setw(12) << revision;
    return id.str();
}
std::string FoundingRejection(GuildCandidate const& c, GuildSettings const& s, std::uint32_t faction)
{
    if (!c.Enrollment.Guid || !c.Enrollment.IdentityValid)
        return "unknown_identity";
    if (c.Enrollment.Revision >= 0xffffffffffffULL)
        return "revision_out_of_range";
    if (!c.Enrollment.AccountIsRandomBot)
        return "not_dedicated_type1_account";
    if (c.Enrollment.Guild)
        return "already_guilded";
    if (c.Enrollment.State != EnrollmentState::Absent && c.Enrollment.State != EnrollmentState::Retired)
        return "already_enrolled";
    if (c.Faction != faction)
        return "incompatible_faction";
    if (!c.Class || c.Class > 11 || !(s.ClassMask & (1u << (c.Class - 1))))
        return "ineligible_class";
    if (!c.Available)
        return c.Reason.empty() ? "activity_conflict" : c.Reason;
    return {};
}
void GuildDirector::Configure(GuildSettings settings)
{
    _settings = settings;
    _cooldown = settings.CooldownMs;
    if (!settings.Valid())
    {
        _settings.Enabled = false;
        Reject("invalid_configuration");
    }
    if (!_settings.Enabled || _settings.DryRun)
    {
        _port.Release(_reserved);
        _reserved = 0;
    }
    _untilStep = 0;
}
void GuildDirector::Reject(std::string reason)
{
    _last = std::move(reason);
    ++_rejections[_last];
}
void GuildDirector::Quarantine(GuildIntent intent, std::string reason)
{
    auto before = intent.Phase;
    intent.Phase = GuildPhase::Quarantined;
    if (_created && intent.Slot == _jobSlot)
        intent.Guild = _created;
    intent.Reason = std::move(reason);
    if (_port.Persist(intent, before))
    {
        _loginRequested = false;
    }
    Reject(intent.Reason);
}
void GuildDirector::Update(std::uint32_t diff)
{
    if (!_port.OnWorldThread())
        return;
    _cooldown = diff >= _cooldown ? 0 : _cooldown - diff;
    if (!_settings.Enabled)
        return;
    if (_jobSlot)
    {
        _jobAge = std::min(600000u, _jobAge + std::min(diff, 600000u));
        _phaseAge = std::min(600000u, _phaseAge + std::min(diff, 600000u));
        if (_reserved && _jobAge < 600000 && !_settings.DryRun && !_port.Reserve(_reserved))
            _reserved = 0;
    }
    if (diff < _untilStep)
    {
        _untilStep -= diff;
        return;
    }
    _untilStep = _settings.IntervalMs;
    if (!_port.Ready())
    {
        Reject("awaiting_enrollment_or_ownership_snapshot");
        return;
    }
    // Release only after the durable snapshot resolves the write, including
    // uncertain acknowledgements. Scheduling a transaction is not a commit.
    for (auto const& item : _port.Intents())
        if (item.first == _jobSlot &&
            (item.second.Phase == GuildPhase::Managed || item.second.Phase == GuildPhase::Quarantined))
        {
            _port.Release(_reserved);
            _reserved = 0;
        }
    bool auditedManaged = false;
    bool unresolved = std::any_of(_port.Intents().begin(), _port.Intents().end(),
                                  [](auto const& item) { return item.second.Phase == GuildPhase::Quarantined; });
    for (auto const& item : _port.Intents())
    {
        auto const& intent = item.second;
        if (intent.Phase == GuildPhase::Quarantined)
        {
            continue;
        }
        if (intent.Phase == GuildPhase::Managed)
        {
            if (auditedManaged || item.first <= _managedCursor)
                continue;
            auditedManaged = true;
            _managedCursor = item.first;
            auto view = _port.Inspect(intent);
            if (!view.Safe)
            {
                if (view.Reason == "founder_snapshot_stale")
                {
                    Reject(view.Reason);
                    return;
                }
                if (!_settings.DryRun)
                    Quarantine(intent, view.Reason);
                else
                    Reject(view.Reason);
                return;
            }
            auto record = _port.Candidate(intent.Founder).Enrollment;
            if (record.State == EnrollmentState::Pending && !_settings.DryRun)
            {
                _port.Confirm(intent, record);
                return;
            }
            continue;
        }
        if (_settings.DryRun)
        {
            _last = std::string("dry_run_recover_slot=") + std::to_string(intent.Slot);
            return;
        }
        if (unresolved && !(_jobSlot == intent.Slot && _created))
        {
            Reject("unresolved_quarantine_blocks_founding");
            return;
        }
        Progress(intent);
        return;
    }
    if (!auditedManaged)
        _managedCursor = 0;
    if (unresolved)
    {
        Reject("unresolved_quarantine_blocks_founding");
        return;
    }
    if (_cooldown)
    {
        _last = "founding_cooldown";
        return;
    }
    std::uint32_t slot = 1;
    while (slot <= _settings.Target && _port.Intents().count(slot))
        ++slot;
    if (slot > _settings.Target)
    {
        _last = "target_slots_reserved";
        return;
    }
    auto faction = NextFoundingFaction(_settings, _port.Intents());
    static char const* names[] = {"Ashen Vanguard", "Amber Covenant", "Silver Horizon",  "Dawn Wardens",
                                  "Iron Accord",    "Crimson Watch",  "Silent Standard", "Twilight Assembly"};
    for (std::uint32_t n = 0; n < _settings.CandidateBudget; ++n)
    {
        auto guid = _port.NextCandidate(_cursor);
        if (!guid)
        {
            _cursor = 0;
            Reject("candidate_page_exhausted");
            return;
        }
        _cursor = guid;
        ++_considered;
        auto candidate = _port.Candidate(guid);
        auto reason = FoundingRejection(candidate, _settings, faction);
        if (!reason.empty())
        {
            Reject(reason);
            continue;
        }
        _last = std::string(_settings.DryRun ? "dry_run_founder=" : "founder=") + std::to_string(guid) +
                " slot=" + std::to_string(slot);
        if (_settings.DryRun)
            return;
        GuildIntent intent{slot,
                           guid,
                           candidate.Enrollment.Account,
                           faction,
                           0,
                           GuildPhase::Planned,
                           names[(slot - 1) % 8],
                           "director founding intent"};
        intent.StageRevision = candidate.Enrollment.Revision;
        intent.StageRequest = GuildRequestId(guid, slot, 0x1001, intent.StageRevision);
        // More than eight slots use distinct valid alphabetic suffixes.
        if (slot > 8)
            intent.Name += std::string(" ") + char('A' + (slot - 1) / 8);
        if (_port.Persist(intent, std::nullopt))
            _cooldown = _settings.CooldownMs;
        return;
    }
}
void GuildDirector::Progress(GuildIntent const& intent)
{
    if (_jobSlot != intent.Slot)
    {
        _jobSlot = intent.Slot;
        _created = _armed = 0;
        _jobAge = 0;
        _phaseAge = 0;
        _progress = 0;
        _loginRequested = false;
        _createAttempted = false;
    }
    auto c = _port.Candidate(intent.Founder);
    unsigned progress = intent.Phase == GuildPhase::Creating ? 3u : intent.StageVerified ? (c.Online ? 2u : 1u) : 0u;
    if (progress > _progress)
    {
        _progress = progress;
        _phaseAge = 0;
    }
    if (_jobAge >= 600000 || _phaseAge >= (intent.Phase == GuildPhase::Creating ? 90000u : 180000u))
    {
        Quarantine(intent, "founding_timeout_pending_retained");
        return;
    }
    // A successful API result must survive stale enrollment snapshots and every
    // scheduling/SQL retry. Never re-enter Create for this intent.
    if (intent.Phase == GuildPhase::Creating && _created)
    {
        auto next = intent;
        next.Guild = _created;
        next.Phase = GuildPhase::Managed;
        next.Reason = "director-created ownership";
        if (!_port.Durable(next))
        {
            Reject("awaiting_core_guild_commit");
            return;
        }
        _port.Persist(next, GuildPhase::Creating);
        return;
    }
    if (!c.Enrollment.IdentityValid)
    {
        Reject("founder_snapshot_stale");
        return;
    }
    if (!c.Enrollment.AccountIsRandomBot || c.Enrollment.Account != intent.Account || c.Faction != intent.Faction)
    {
        Quarantine(intent, "founder_identity_changed");
        return;
    }
    if (intent.Phase == GuildPhase::Creating)
    {
        if (_armed != intent.Slot)
        {
            Quarantine(intent, "interrupted_create_requires_review");
            return;
        }
        if (!_created)
        {
            if (_createAttempted)
            {
                Quarantine(intent, "guild_api_failed_or_conflicted");
                return;
            }
            if (!_reserved || !_port.Reserve(_reserved))
            {
                Quarantine(intent, "founding_reservation_lost");
                return;
            }
            if (!c.Available || !c.Online || c.Enrollment.Guild)
            {
                Quarantine(intent, "founder_unavailable_at_create");
                return;
            }
            _createAttempted = true;
            _created = _port.Create(intent);
            if (!_created)
            {
                Quarantine(intent, "guild_api_failed_or_conflicted");
                return;
            }
            _phaseAge = 0;
            return;  // Core guild/member commits use a separate async pool.
        }
    }
    if (c.Enrollment.Guild)
    {
        Quarantine(intent, "unexpected_existing_guild_no_adoption");
        return;
    }
    if (!_reserved)
    {
        if (!_port.Reserve(intent.Founder))
        {
            Reject("enrollment_reservation_busy");
            return;
        }
        _reserved = intent.Founder;
    }
    if (c.Enrollment.State == EnrollmentState::Absent || c.Enrollment.State == EnrollmentState::Retired)
    {
        if (!_port.Offline(intent.Founder))
        {
            Reject("awaiting_normal_logout_save");
            return;
        }
        _port.Stage(intent, c.Enrollment);
        return;
    }
    if (c.Enrollment.State != EnrollmentState::Pending)
    {
        Quarantine(intent, "unexpected_founder_enrollment");
        return;
    }
    if (!intent.StageVerified || c.Enrollment.Revision != intent.StageRevision + 1)
    {
        Reject("awaiting_exact_director_stage_receipt");
        return;
    }
    if (!c.Online)
    {
        if (!_loginRequested)
            _loginRequested = _port.Login(intent.Founder);
        Reject("awaiting_protected_founder_login");
        return;
    }
    if (!c.Available)
    {
        Reject(c.Reason);
        return;
    }
    auto next = intent;
    next.Phase = GuildPhase::Creating;
    next.Reason = "authorized internal guild creation";
    if (_port.Persist(next, GuildPhase::Planned))
        _armed = intent.Slot;
}
}  // namespace PlayerbotAutonomy
