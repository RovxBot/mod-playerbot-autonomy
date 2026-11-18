#include "GuildRuntime.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>

#include "CharacterCache.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
namespace PlayerbotAutonomy
{
GuildRuntime& Guilds()
{
    static GuildRuntime runtime;
    return runtime;
}
void GuildRuntime::Configure(GuildSettings settings)
{
    _director.Configure(settings);
    _attempted = _opened;
}
void GuildRuntime::Start(std::string const& info)
{
    _owner = std::this_thread::get_id();
    _info = info;
    if (!_director.Settings().Enabled || _opened)
        return;
    _attempted = true;
    _opened = _store.Initialize(info);
    if (!_opened)
    {
        ++_failures;
        LOG_ERROR("module", "autonomy guild store unavailable; founding disabled, enrollment protection retained");
    }
}
void GuildRuntime::Stop()
{
    _director.Configure(GuildSettings{});
    _loaded = false;
    _query.reset();
    _transaction.reset();
    _store.Close();
    _opened = _attempted = false;
    _intents.clear();
    _auditDone = false;
    _auditCursor = _existingGuilds = 0;
    _needLoad = true;
}
bool GuildRuntime::Ready() const
{
    return std::this_thread::get_id() == _owner && _opened && _loaded && _auditDone && !_needLoad && !_query &&
           !_transaction && Runtime().Idle() && Runtime().ActivityEnabled() &&
           sRandomPlayerbotMgr.DedicatedAccountSnapshotReady();
}
void GuildRuntime::Update(uint32 diff)
{
    if (std::this_thread::get_id() != _owner)
        return;
    auto start = std::chrono::steady_clock::now();
    auto receipts = Runtime().Counters().Committed + Runtime().Counters().Replayed;
    if (_enrollmentReceipts != receipts)
    {
        _enrollmentReceipts = receipts;
        ++_snapshotEpoch;
        _needLoad = true;
    }
    if (!_attempted && _director.Settings().Enabled)
        Start(_info);
    if (_query || _transaction)
    {
        _queryAge = std::min(30001u, _queryAge + std::min(diff, 30001u));
        if (_queryAge > 30000)
        {
            _loaded = false;
            _needLoad = true;
        }
        if (_query && _query->InvokeIfReady())
            _query.reset();
        if (_transaction && _transaction->InvokeIfReady())
            _transaction.reset();
    }
    else if (_opened)
    {
        _queryAge = 0;
        _retry = diff >= _retry ? 0 : _retry - diff;
        if (!_retry)
        {
            if (_needLoad)
                Load();
            else if (!_auditDone)
                AuditNext();
        }
    }
    _director.Update(diff);
    ++_ticks;
    auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    _maxTickUs = std::max(_maxTickUs, static_cast<uint64>(elapsed));
}
void GuildRuntime::Load()
{
    auto epoch = _snapshotEpoch;
    _query.emplace(_store.Load().WithPreparedCallback(
        [this, epoch](PreparedQueryResult result)
        {
            if (!result)
            {
                _loaded = false;
                _retry = 5000;
                ++_failures;
                return;
            }
            if (epoch != _snapshotEpoch)
                return;
            std::map<uint32, GuildIntent> intents;
            do
            {
                auto* f = result->Fetch();
                if (f[0].IsNull())
                    break;
                GuildIntent m;
                m.Slot = f[0].Get<uint32>();
                m.Founder = f[1].Get<uint32>();
                m.Account = f[2].Get<uint32>();
                m.Faction = f[3].Get<uint32>();
                m.Guild = f[4].Get<uint32>();
                auto phase = f[5].Get<std::string>();
                m.Phase = GuildPhase::Quarantined;
                for (auto value : {GuildPhase::Planned, GuildPhase::Creating, GuildPhase::Managed})
                    if (phase == PhaseName(value))
                        m.Phase = value;
                m.Name = f[6].Get<std::string>();
                m.Reason = f[7].Get<std::string>();
                m.StageRevision = f[8].Get<uint64>();
                m.StageRequest = f[9].Get<std::string>();
                m.StageVerified = f[10].Get<uint32>() != 0;
                if (!m.Slot || m.Slot > 64 || !m.Founder || !m.Account || m.Faction > 1)
                {
                    _loaded = false;
                    _retry = 5000;
                    ++_failures;
                    return;
                }
                intents.emplace(m.Slot, std::move(m));
            } while (result->NextRow());
            _intents = std::move(intents);
            _loaded = true;
            _needLoad = false;
        }));
}
void GuildRuntime::AuditNext()
{
    _query.emplace(_store.AuditPage(_auditCursor)
                       .WithPreparedCallback(
                           [this](PreparedQueryResult result)
                           {
                               if (!result)
                               {
                                   _retry = 5000;
                                   ++_failures;
                                   return;
                               }
                               auto* f = result->Fetch();
                               if (f[0].IsNull())
                               {
                                   _auditDone = true;
                                   return;
                               }
                               _auditCursor = f[0].Get<uint32>();
                               ++_existingGuilds;
                               LOG_INFO("module", "{}", Audit(_auditCursor));
                           }));
}
bool GuildRuntime::Persist(GuildIntent const& next, std::optional<GuildPhase> before)
{
    if (!Ready() || _director.Settings().DryRun || !_director.Settings().Enabled)
        return false;
    _transaction.emplace(_store.Save(next, before));
    _transaction->AfterComplete(
        [this](bool success)
        {
            if (!success)
            {
                ++_failures;
                _retry = 5000;
            }
            _needLoad = true;
            _loaded = false;  // Resolve uncertain commit by reading durable intent, never retry Create.
        });
    return true;
}
GuildCandidate GuildRuntime::Candidate(uint32 lowGuid) const
{
    GuildCandidate c;
    c.Enrollment = Runtime().Cache().Get(lowGuid);
    if (std::this_thread::get_id() != _owner)
    {
        c.Reason = "wrong_world_thread";
        return c;
    }
    auto guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
    auto* entry = sCharacterCache->GetCharacterCacheByGuid(guid);
    if (!entry || entry->AccountId != c.Enrollment.Account || entry->GuildId != c.Enrollment.Guild)
    {
        c.Enrollment.IdentityValid = false;
        c.Reason = "character_cache_mismatch";
        return c;
    }
    if (sWorldSessionMgr->FindSession(entry->AccountId))
    {
        c.Reason = "human_account_session";
        return c;
    }
    c.Class = entry->Class;
    c.Faction = sCharacterCache->GetCharacterTeamByGuid(guid);
    c.Enrollment.AccountIsRandomBot = sRandomPlayerbotMgr.IsDedicatedRandomAccount(entry->AccountId);
    if (!entry->GroupGuid.IsEmpty())
    {
        c.Reason = "group_conflict";
        return c;
    }
    auto* player = ObjectAccessor::FindConnectedPlayer(guid);
    c.Online = player != nullptr;
    if (!player)
    {
        c.Available = !PlayerbotHolder::HasPendingBotLogin(guid);
        c.Reason = c.Available ? "eligible_offline" : "login_in_flight";
        return c;
    }
    auto* ai = GET_PLAYERBOT_AI(player);
    if (!player->GetSession() || player->GetSession()->GetAccountId() != entry->AccountId || !ai ||
        IsRealPlayer(player) || IsSelfBot(player) || ai->HasGameClientMaster() ||
        sRandomPlayerbotMgr.GetPlayerBot(guid) != player)
    {
        c.Reason = "human_selfbot_or_unowned_session";
        return c;
    }
    if (player->GetGroup() || player->GetOriginalGroup())
    {
        c.Reason = "group_conflict";
        return c;
    }
    if (player->GetGuildIdInvited())
    {
        c.Reason = "pending_guild_invite";
        return c;
    }
    if (!player->IsAlive() || player->IsInCombat() || player->IsBeingTeleported() || player->IsInFlight() ||
        player->InBattleground() || player->InArena() || player->InBattlegroundQueue() || !player->IsInWorld() ||
        !player->GetMap() || player->GetMap()->IsDungeon())
    {
        c.Reason = "combat_travel_or_instance_conflict";
        return c;
    }
    c.Available = true;
    c.Reason = "eligible_online";
    return c;
}
GuildView GuildRuntime::Inspect(GuildIntent const& m) const
{
    GuildView view;
    view.Guild = m.Guild;
    if (!sRandomPlayerbotMgr.DedicatedAccountSnapshotReady())
    {
        view.Reason = "dedicated_account_snapshot_unavailable";
        return view;
    }
    auto recorded = _intents.find(m.Slot);
    if (recorded == _intents.end() || recorded->second.Phase != m.Phase || recorded->second.Guild != m.Guild ||
        recorded->second.Founder != m.Founder || recorded->second.Account != m.Account ||
        recorded->second.Name != m.Name || recorded->second.StageRequest != m.StageRequest ||
        !recorded->second.StageVerified)
    {
        view.Reason = "no_verified_explicit_ownership";
        return view;
    }
    auto* guild = sGuildMgr->GetGuildById(m.Guild);
    if (!guild)
    {
        view.Reason = "missing_guild";
        return view;
    }
    view.Leader = guild->GetLeaderGUID().GetCounter();
    view.Members = guild->GetMemberCount();
    view.Faction = m.Faction;
    if (guild->GetName() == "Git Commit" || guild->GetName() == "Bangerzoids")
    {
        view.Reason = "human_managed_guild";
        return view;
    }
    if (m.Phase != GuildPhase::Managed || guild->GetName() != m.Name || view.Leader != m.Founder ||
        !guild->GetMember(guild->GetLeaderGUID()))
    {
        view.Reason = "ownership_identity_mismatch";
        return view;
    }
    if (view.Members > _director.Settings().MaxMembers)
    {
        view.Reason = "membership_cap_exceeded";
        return view;
    }
    for (auto const& item : guild->GetUnorderedMembers())
    {
        auto guid = ObjectGuid::Create<HighGuid::Player>(item.first);
        if (sWorldSessionMgr->FindSession(sCharacterCache->GetCharacterAccountIdByGuid(guid)))
        {
            view.Reason = "human_account_session";
            return view;
        }
        if (!sRandomPlayerbotMgr.IsDedicatedRandomAccount(sCharacterCache->GetCharacterAccountIdByGuid(guid)))
        {
            view.Reason = "human_or_addclass_member";
            return view;
        }
        if (sCharacterCache->GetCharacterTeamByGuid(guid) != m.Faction)
        {
            view.Reason = "mixed_faction_membership";
            return view;
        }
        auto* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (player && (IsRealPlayer(player) || IsSelfBot(player) || !GET_PLAYERBOT_AI(player) ||
                       GET_PLAYERBOT_AI(player)->HasGameClientMaster()))
        {
            view.Reason = "human_controlled_member";
            return view;
        }
    }
    if (!m.StageVerified)
    {
        view.Reason = "ownership_stage_receipt_missing";
        return view;
    }
    auto founder = Candidate(m.Founder);
    if (!founder.Enrollment.IdentityValid || founder.Enrollment.Account != m.Account ||
        founder.Enrollment.Guild != m.Guild)
    {
        view.Reason = "founder_snapshot_stale";
        return view;
    }
    view.Safe = true;
    view.Reason = "explicit_managed_ownership_verified";
    return view;
}
bool GuildRuntime::Submit(GuildIntent const& m, EnrollmentRecord const& before, EnrollmentRequest action)
{
    TransitionRequest request;
    request.Guid = m.Founder;
    request.Guild = action == EnrollmentRequest::ConfirmAfterGuildJoin ? m.Guild : 0;
    request.ExpectedRevision = action == EnrollmentRequest::StageBeforeGuildJoin ? m.StageRevision : before.Revision;
    request.Action = action;
    request.RequestId =
        GuildRequestId(m.Founder, m.Slot, action == EnrollmentRequest::StageBeforeGuildJoin ? 0x1001 : 0x1002,
                       request.ExpectedRevision);
    request.Reason = "director founding slot " + std::to_string(m.Slot);
    request.Director = true;
    request.ActorGuid = m.Founder;
    return Runtime().Submit(std::move(request));
}
bool GuildRuntime::Stage(GuildIntent const& m, EnrollmentRecord const& before)
{
    return Submit(m, before, EnrollmentRequest::StageBeforeGuildJoin);
}
bool GuildRuntime::Confirm(GuildIntent const& m, EnrollmentRecord const& before)
{
    if (!Inspect(m).Safe)
        return false;
    return Submit(m, before, EnrollmentRequest::ConfirmAfterGuildJoin);
}
uint32 GuildRuntime::Create(GuildIntent const& m)
{
    auto recorded = _intents.find(m.Slot);
    if (recorded == _intents.end() || recorded->second.Phase != GuildPhase::Creating ||
        recorded->second.Founder != m.Founder || recorded->second.Account != m.Account ||
        recorded->second.Name != m.Name || recorded->second.StageRequest != m.StageRequest ||
        !recorded->second.StageVerified)
        return 0;
    auto c = Candidate(m.Founder);
    if (!Ready() || _director.Settings().DryRun || m.Phase != GuildPhase::Creating || !c.Available || !c.Online ||
        c.Enrollment.State != EnrollmentState::Pending || c.Enrollment.Guild || !m.StageVerified ||
        !c.Enrollment.AccountIsRandomBot || c.Enrollment.Account != m.Account || c.Faction != m.Faction || !c.Class ||
        c.Class > 11 || !(_director.Settings().ClassMask & (1u << (c.Class - 1))) ||
        !ObjectMgr::IsValidCharterName(m.Name) || sGuildMgr->GetGuildByName(m.Name))
        return 0;
    auto guild = std::make_unique<Guild>();
    auto* founder = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(m.Founder));
    if (!founder || !guild->Create(founder, m.Name))
        return 0;
    uint32 id = guild->GetId();
    sGuildMgr->AddGuild(guild.release());
    Runtime().Invalidate(m.Founder);
    return id;
}
std::string GuildRuntime::Audit(uint32 id) const
{
    std::ostringstream out;
    if (!id)
    {
        out << "guild_audit loaded=" << _loaded << " startup_audit_complete=" << _auditDone
            << " existing_guilds_observed=" << _existingGuilds << " intents=" << _intents.size()
            << " failures=" << _failures << " max_tick_us=" << _maxTickUs << " decision=" << _director.LastDecision();
        return out.str();
    }
    auto* guild = sGuildMgr->GetGuildById(id);
    out << "guild=" << id;
    if (!guild)
        return out.str() + " reason=missing_guild";
    out << " name=" << guild->GetName() << " members=" << guild->GetMemberCount();
    for (auto const& item : _intents)
        if (item.second.Guild == id)
        {
            auto view = Inspect(item.second);
            out << " phase=" << PhaseName(item.second.Phase) << " reason=" << view.Reason;
            return out.str();
        }
    return out.str() + " managed=false reason=no_explicit_ownership_record; never_adopted";
}
std::string GuildRuntime::Plan(uint32 guid, uint32 slot) const
{
    if (!slot || slot > _director.Settings().Target)
        return "reason=invalid_slot";
    auto faction = NextFoundingFaction(_director.Settings(), _intents);
    auto reason = FoundingRejection(Candidate(guid), _director.Settings(), faction);
    if (_intents.count(slot))
        reason = "slot_already_reserved";
    return "founding_dry_run guid=" + std::to_string(guid) + " slot=" + std::to_string(slot) +
           " reason=" + (reason.empty() ? "eligible" : reason) + "; nothing_queued";
}
}  // namespace PlayerbotAutonomy
