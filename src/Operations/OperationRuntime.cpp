#include "OperationRuntime.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>
#include <sstream>

#include "AiFactory.h"
#include "CharacterCache.h"
#include "Guild.h"
#include "Guild/GuildRuntime.h"
#include "GuildMgr.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

namespace PlayerbotAutonomy
{
OperationRuntime& Operations()
{
    static OperationRuntime runtime;
    return runtime;
}
void OperationRuntime::Configure(OperationSettings settings)
{
    _settings = settings;
    if (!settings.Valid())
    {
        _settings.Enabled = false;
        _decision = settings.AssembleGroups ? "group_executor_unavailable" : "invalid_configuration";
    }
    _planIn = _refreshIn = 0;
    if (!_settings.Enabled)
    {
        _roster.clear();
        _completeGuilds.clear();
        _refreshGuids.clear();
        _refreshAt = 0;
    }
}
void OperationRuntime::Start(std::string const& info)
{
    _owner = std::this_thread::get_id();
    _now = _clock ? _clock() : static_cast<uint64>(std::time(nullptr));
    _elapsedMs = 0;
    // Recovery runs even with activities off. This pool touches module tables
    // only, and all SQL after initialization uses the asynchronous worker.
    _opened = _store.Initialize(info);
    _recovered = false;
    _recoveryCursor = 0;
    if (!_opened)
        _decision = "operation_store_unavailable";
}
RosterMember OperationRuntime::Observe(uint32 guid, uint32 guild, bool managed) const
{
    RosterMember m;
    m.Guid = guid;
    m.Guild = guild;
    m.Managed = managed;
    m.ObservedAt = _now;
    if (!OnWorldThread())
        return m;
    auto c = Guilds().Candidate(guid);
    m.Account = c.Enrollment.Account;
    m.Faction = c.Faction;
    m.Class = c.Class;
    m.Identity = c.Enrollment.IdentityValid && c.Enrollment.Guild == guild;
    m.Persistent = c.Enrollment.State == EnrollmentState::Persistent;
    m.Dedicated = c.Enrollment.AccountIsRandomBot;
    m.Online = c.Online;
    m.Available = c.Available;
    m.Restriction = c.Reason;
    m.Reservation = Runtime().Cache().OperationOwner(guid);
    if (Runtime().Cache().Reserved() == guid)
        m.Reservation = UINT64_MAX;
    auto objectGuid = ObjectGuid::Create<HighGuid::Player>(guid);
    auto* entry = sCharacterCache->GetCharacterCacheByGuid(objectGuid);
    if (entry)
        m.Level = entry->Level;
    m.Grouped = entry && !entry->GroupGuid.IsEmpty();
    auto* player = ObjectAccessor::FindConnectedPlayer(objectGuid);
    m.HumanControlled = !c.Available && c.Reason.find("human") != std::string::npos;
    if (!player)
        return m;  // Offline role/equipment evidence remains unknown.
    auto* ai = GET_PLAYERBOT_AI(player);
    if (!ai || !c.Enrollment.AccountIsRandomBot || IsRealPlayer(player) || IsSelfBot(player) ||
        ai->HasGameClientMaster() || sRandomPlayerbotMgr.GetPlayerBot(objectGuid) != player)
    {
        m.HumanControlled = true;
        return m;
    }
    auto* actualGuild = sGuildMgr->GetGuildById(guild);
    m.Identity = m.Identity && player->GetGuildId() == guild && actualGuild && actualGuild->GetMember(objectGuid);
    m.Level = player->GetLevel();
    m.Grouped = player->GetGroup() || player->GetOriginalGroup() || (entry && !entry->GroupGuid.IsEmpty());
    m.Queued = player->InBattlegroundQueue() || sLFGMgr->GetState(objectGuid) != lfg::LFG_STATE_NONE;
    m.InInstance = !player->GetMap() || player->GetMap()->IsDungeon() || player->InBattleground() || player->InArena();
    auto tabs = AiFactory::GetPlayerSpecTabs(player);
    uint32 maximum = 0, winners = 0;
    for (auto const& tab : tabs)
        if (tab.second > maximum)
        {
            maximum = tab.second;
            winners = 1;
        }
        else if (tab.second == maximum)
            ++winners;
    m.Spec = AiFactory::GetPlayerSpecTab(player);
    // Strategy and spec must agree. No role is inferred just from class, and
    // untalented/tied specs do not inherit AiFactory's fallback choice.
    bool unique = maximum > 0 && winners == 1;
    auto strategyRole =
        EvidenceRole(PlayerbotAI::IsTank(player), PlayerbotAI::IsHeal(player), PlayerbotAI::IsDps(player), unique);
    auto specRole = EvidenceRole(PlayerbotAI::IsTank(player, true), PlayerbotAI::IsHeal(player, true),
                                 PlayerbotAI::IsDps(player, true), unique);
    m.Role = strategyRole == specRole ? strategyRole : CombatRole::Unknown;
    m.EquippedItemLevel = ai->GetEquipGearScore(player);  // Actual equipped average, not fabricated gearscore.
    auto* main = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    m.EquipmentKnown = main && !main->IsBroken() && m.EquippedItemLevel > 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (auto* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (item->IsBroken())
            {
                m.EquipmentKnown = false;
                m.Restriction = "broken_equipment";
            }
    for (uint32 skill : {164u, 165u, 171u, 182u, 186u, 197u, 202u, 333u, 393u, 755u, 773u})
        if (auto value = player->GetPureSkillValue(skill))
            m.Professions[skill] = value;
    return m;
}
void OperationRuntime::Refresh()
{
    if (!Guilds().Ready())
    {
        _decision = "ownership_or_m1_snapshot_unready";
        return;
    }
    if (_refreshAt >= _refreshGuids.size())
    {
        _refreshGuids.clear();
        _refreshAt = 0;
        _refreshOverflow = false;
        auto it = Guilds().Intents().upper_bound(_guildCursor);
        if (it == Guilds().Intents().end())
        {
            _guildCursor = 0;
            it = Guilds().Intents().begin();
        }
        if (it == Guilds().Intents().end())
            return;
        _guildCursor = it->first;
        auto const& intent = it->second;
        _refreshGuild = intent.Guild;
        auto view = Guilds().Inspect(intent);
        _refreshSafe = intent.Phase == GuildPhase::Managed && view.Safe;
        if (!_refreshSafe)
        {
            for (auto m = _roster.begin(); m != _roster.end();)
                if (m->second.Guild == _refreshGuild)
                    m = _roster.erase(m);
                else
                    ++m;
            _completeGuilds.erase(_refreshGuild);
            _decision = view.Reason;
            return;
        }
        auto* guild = sGuildMgr->GetGuildById(_refreshGuild);
        if (!guild)
            return;
        for (auto const& member : guild->GetUnorderedMembers())
            _refreshGuids.push_back(member.first);
        std::sort(_refreshGuids.begin(), _refreshGuids.end());
        for (auto m = _roster.begin(); m != _roster.end();)
            if (m->second.Guild == _refreshGuild &&
                !std::binary_search(_refreshGuids.begin(), _refreshGuids.end(), m->first))
                m = _roster.erase(m);
            else
                ++m;
    }
    for (uint32 n = 0; n < _settings.RefreshBudget && _refreshAt < _refreshGuids.size(); ++n)
    {
        auto guid = _refreshGuids[_refreshAt++];
        // Never grow beyond the configured planner bound, even with 64 guilds.
        if (_roster.size() >= _settings.Rules.MaxCandidates && !_roster.count(guid))
        {
            _decision = "roster_capacity_exceeded";
            _refreshOverflow = true;
            _completeGuilds.erase(_refreshGuild);
            break;
        }
        _roster[guid] = Observe(guid, _refreshGuild, _refreshSafe);
    }
    if (_refreshAt == _refreshGuids.size() && !_refreshOverflow)
        _completeGuilds.insert(_refreshGuild);
}
PartyPlan OperationRuntime::Plan(uint32 guild) const
{
    if (!_completeGuilds.count(guild))
    {
        PartyPlan incomplete;
        incomplete.Guild = guild;
        incomplete.Reason = "roster_refresh_incomplete_or_unmanaged";
        return incomplete;
    }
    std::vector<RosterMember> candidates;
    uint32 faction = 2;
    for (auto const& item : Guilds().Intents())
        if (item.second.Guild == guild && item.second.Phase == GuildPhase::Managed)
            faction = item.second.Faction;
    for (auto const& item : _roster)
        if ((item.second.Guild == guild || _settings.Rules.CrossGuild) && _completeGuilds.count(item.second.Guild))
        {
            auto m = item.second;
            m.Reservation = Runtime().Cache().OperationOwner(m.Guid);
            if (Runtime().Cache().Reserved() == m.Guid)
                m.Reservation = UINT64_MAX;
            candidates.push_back(std::move(m));
        }
    return PlanParty(candidates, _settings.Rules, guild, faction, _now);
}
bool OperationRuntime::Validate(Operation const& operation, bool requireLease) const
{
    if (!Guilds().Ready() || !Runtime().ProtectionAvailable())
        return false;
    std::set<uint32> guilds;
    for (auto const& m : operation.Members)
        guilds.insert(m.Guild);
    for (auto guild : guilds)
    {
        bool safe = false;
        for (auto const& item : Guilds().Intents())
            if (item.second.Guild == guild && Guilds().Inspect(item.second).Safe)
                safe = true;
        if (!safe)
            return false;
    }
    uint32 low = 80, high = 0;
    for (auto const& member : operation.Members)
    {
        auto m = Observe(member.Guid, member.Guild, true);
        if (!RosterRejection(m, _settings.Rules, operation.Guild, operation.Faction, _now, operation.Id).empty() ||
            m.Account != member.Account || m.Role != member.Role || m.Reservation != (requireLease ? operation.Id : 0))
            return false;
        low = std::min(low, m.Level);
        high = std::max(high, m.Level);
    }
    return high - low <= _settings.Rules.LevelSpread;
}
void OperationRuntime::Save(Operation next, Operation const* before)
{
    _before = before ? std::optional<Operation>(*before) : std::nullopt;
    _pending = std::move(next);
    ++_writes;
    _age = 0;
    _transaction.emplace(_store.Save(*_pending, _before ? &*_before : nullptr));
    _transaction->AfterComplete(
        [this](bool success)
        {
            if (!success)
                ++_failures;
            _readback = true;  // Never publish or release on an acknowledgement alone.
        });
}
void OperationRuntime::Read(uint64 id)
{
    ++_queries;
    _query.emplace(_store.Load(id).WithPreparedCallback(
        [this, id](PreparedQueryResult result)
        {
            Operation durable;
            uint64 leases = 0;
            if (!OperationStore::Decode(result, durable, leases))
            {
                ++_failures;
                // A real sentinel is confirmed absence. A failed/corrupt read is
                // uncertainty and retains the intent for bounded retry/recovery.
                if (result && result->Fetch()[0].IsNull() && _pending && !_before)
                {
                    Runtime().Cache().ReleaseOperation(id);
                    _pending.reset();
                    _decision = "proposal_not_committed";
                }
                else
                {
                    _readback = true;
                    _retryIn = 5000;
                }
                return;
            }
            bool expectedLeases = durable.State == OperationState::Reserved ||
                                  durable.State == OperationState::Assembling ||
                                  durable.State == OperationState::Active || durable.State == OperationState::Closing;
            if ((expectedLeases && leases != durable.Members.size()) || (Terminal(durable.State) && leases))
            {
                ++_failures;
                _decision = "durable_lease_mismatch";
                _operations[id] = durable;
                _recoverId = id;
                _pending.reset();
                _before.reset();
                return;
            }
            if (_pending)
            {
                if (!SameOperation(durable, *_pending))
                {
                    if (_before && SameOperation(durable, *_before))
                    {
                        // Failed mutation: retain the committed state, then close
                        // safely rather than retrying an acquisition indefinitely.
                        _operations[id] = durable;
                        _decision = "write_rejected_recovering";
                        _recoverId = id;
                        _pending.reset();
                        _before.reset();
                        return;
                    }
                    ++_failures;
                    _decision = "id_or_payload_conflict";
                    _readback = true;
                    _retryIn = 5000;
                    return;
                }
                _pending.reset();
                _before.reset();
            }
            if (Terminal(durable.State))
            {
                Runtime().Cache().ReleaseOperation(id);
                _operations.erase(id);
                _decision = durable.Reason;
                if (_recoverId == id)
                    _recoverId = 0;
            }
            else
            {
                _operations[id] = std::move(durable);
                if (!_recovered)
                {
                    _recoverId = id;
                    ++_recoveries;
                }
            }
        }));
}
void OperationRuntime::RecoverNext()
{
    ++_queries;
    _query.emplace(_store.RecoverPage(_recoveryCursor)
                       .WithPreparedCallback(
                           [this](PreparedQueryResult result)
                           {
                               if (!result)
                               {
                                   ++_failures;
                                   _retryIn = 5000;
                                   return;
                               }
                               if (result->Fetch()[0].IsNull())
                               {
                                   _recovered = true;
                                   return;
                               }
                               _recoverId = _recoveryCursor = result->Fetch()[0].Get<uint64>();
                               _readback = true;
                           }));
}
void OperationRuntime::Update(uint32 diff)
{
    if (!OnWorldThread() || !_opened)
        return;
    auto start = std::chrono::steady_clock::now();
    _elapsedMs += diff;
    _now += _elapsedMs / 1000;
    _elapsedMs %= 1000;
    _now = std::max(_now, _clock ? _clock() : static_cast<uint64>(std::time(nullptr)));
    // Absolute leases are never renewed. Local cleanup does not erase M1.
    for (auto const& item : _operations)
        if (_now >= item.second.LeaseUntil)
            Runtime().Cache().ReleaseOperation(item.first);
    if (_pending && _now >= _pending->LeaseUntil)
        Runtime().Cache().ReleaseOperation(_pending->Id);
    _retryIn = diff >= _retryIn ? 0 : _retryIn - diff;
    if (_query || _transaction)
    {
        _age += diff;
        if (_age > 30000)
            _decision = "database_timeout_awaiting_readback";
        if (_query && _query->InvokeIfReady())
            _query.reset();
        if (_transaction && _transaction->InvokeIfReady())
            _transaction.reset();
    }
    else if (_retryIn)
    { /* Backoff only scheduling; callbacks and lease expiry continue. */
    }
    else if (_readback)
    {
        _readback = false;
        _age = 0;
        Read(_pending ? _pending->Id : _recoverId);
    }
    else if (_recoverId && _operations.count(_recoverId))
    {
        auto current = _operations.at(_recoverId);
        auto next = current;
        if (current.State != OperationState::Recovering)
        {
            next.State = OperationState::Recovering;
            ++next.Revision;
            ++next.Attempts;
            next.ChangedAt = _now;
            next.Reason = "interrupted_simulation";
        }
        else
            next = NextOperation(current, _now, false, false);
        Save(std::move(next), &current);
    }
    else if (!_recovered)
        RecoverNext();
    else if (diff >= _planIn)
    {
        _planIn = _settings.IntervalMs;
        if (!_operations.empty() && (!_planTurn || _operations.size() >= _settings.MaxConcurrent || !_settings.Enabled))
        {
            auto it = _operations.upper_bound(_opCursor);
            if (it == _operations.end())
                it = _operations.begin();
            _opCursor = it->first;
            auto current = it->second;
            // Unready enrollment/ownership is a rejection, never an authority
            // to continue. Every phase independently revalidates participants.
            auto next = NextOperation(current, _now, _settings.Enabled && !_settings.DryRun, Validate(current));
            Save(std::move(next), &current);
            _planTurn = true;
        }
        else if (_settings.Enabled && Guilds().Ready())
        {
            _planTurn = false;
            auto it = Guilds().Intents().upper_bound(_planningGuild);
            if (it == Guilds().Intents().end())
            {
                _planningGuild = 0;
                it = Guilds().Intents().begin();
            }
            if (it != Guilds().Intents().end())
            {
                _planningGuild = it->first;
                auto plan = Plan(it->second.Guild);
                _decision = plan.Reason;
                if (plan.Complete() && !_settings.DryRun && _operations.size() < _settings.MaxConcurrent)
                {
                    Operation op;
                    do
                    {
                        op.Id = _ids() & 0x7fffffffffffffffULL;
                    } while (!op.Id);
                    op.Guild = plan.Guild;
                    op.Faction = plan.Faction;
                    op.Leader = plan.Leader;
                    op.Members = std::move(plan.Members);
                    op.CreatedAt = op.ChangedAt = _now;
                    op.Deadline = _now + _settings.TimeoutSeconds;
                    op.LeaseUntil = _now + _settings.LeaseSeconds;
                    if (Validate(op, false) && Runtime().Cache().AcquireOperation(op.Id, ParticipantGuids(op)))
                        Save(std::move(op), nullptr);
                    else
                    {
                        ++_conflicts;
                        _decision = "reservation_conflict";
                    }
                }
            }
        }
    }
    else
        _planIn -= diff;
    if (_settings.Enabled)
    {
        if (diff >= _refreshIn)
        {
            _refreshIn = 1000;
            Refresh();
        }
        else
            _refreshIn -= diff;
    }
    ++_ticks;
    _lastTickUs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    _maxTickUs = std::max(_maxTickUs, _lastTickUs);
}
void OperationRuntime::Stop()
{
    if (!OnWorldThread())
        return;
    // Drain already queued SQL. Unacknowledged intents are recovered next boot;
    // shutdown never waits synchronously for a newly requested game mutation.
    _query.reset();
    _transaction.reset();
    _store.Close();
    for (auto const& item : _operations)
        Runtime().Cache().ReleaseOperation(item.first);
    if (_pending)
        Runtime().Cache().ReleaseOperation(_pending->Id);
    _pending.reset();
    _before.reset();
    _operations.clear();
    _roster.clear();
    _completeGuilds.clear();
    _refreshGuids.clear();
    _refreshAt = 0;
    _opened = false;
    _recovered = false;
    _readback = false;
    _recoverId = 0;
}
std::string OperationRuntime::Diagnose(uint32 guild) const
{
    if (!OnWorldThread())
        return "reason=wrong_world_thread";
    std::ostringstream out;
    out << "m2 enabled=" << _settings.Enabled << " dry_run=" << _settings.DryRun
        << " assembly=simulated store=" << _opened << " recovered=" << _recovered << " snapshots=" << _roster.size()
        << " operations=" << _operations.size() << " pending_write=" << bool(_pending) << " queries=" << _queries
        << " writes=" << _writes << " failures=" << _failures << " lease_conflicts=" << _conflicts
        << " recoveries=" << _recoveries << " ticks=" << _ticks << " last_tick_us=" << _lastTickUs
        << " max_tick_us=" << _maxTickUs << " decision=" << _decision;
    uint32 reserved = 0;
    for (auto const& item : _operations)
    {
        auto const& op = item.second;
        for (auto guid : ParticipantGuids(op))
            reserved += Runtime().Cache().OperationOwner(guid) == op.Id;
        out << " [id=" << op.Id << " guild=" << op.Guild << " state=" << OperationStateName(op.State)
            << " age_s=" << (_now >= op.CreatedAt ? _now - op.CreatedAt : 0) << " reason=" << op.Reason << ']';
    }
    if (_pending)
    {
        if (!_operations.count(_pending->Id))
            for (auto guid : ParticipantGuids(*_pending))
                reserved += Runtime().Cache().OperationOwner(guid) == _pending->Id;
        out << " [pending_id=" << _pending->Id << " state=" << OperationStateName(_pending->State)
            << " outcome=awaiting_durable_readback]";
    }
    out << " reserved=" << reserved;
    if (guild)
    {
        auto plan = Plan(guild);
        uint32 online = 0, equipment = 0, professions = 0;
        for (auto const& item : _roster)
            if (item.second.Guild == guild)
            {
                online += item.second.Online;
                equipment += item.second.EquipmentKnown;
                professions += !item.second.Professions.empty();
            }
        // Recheck selected members without refreshing/queuing the cache. A
        // command must not describe a stale former bot as ready right now.
        for (auto const& member : plan.Members)
        {
            auto current = Observe(member.Guid, member.Guild, true);
            auto reason = RosterRejection(current, _settings.Rules, guild, plan.Faction, _now);
            if (reason.empty() && current.Role != member.Role)
                reason = "role_changed";
            if (!reason.empty())
            {
                ++plan.Rejections[reason];
                plan.Reason = "selected_members_changed_refresh_required";
            }
        }
        out << " guild=" << guild << " tanks=" << plan.Tanks << " healers=" << plan.Healers << " dps=" << plan.Dps
            << " plan=" << plan.Reason << " online_snapshots=" << online << " equipment_known=" << equipment
            << " profession_capable=" << professions;
        for (auto const& reason : plan.Rejections)
            out << " reject_" << reason.first << '=' << reason.second;
        if (plan.Complete() && Guilds().Ready())
            for (auto const& m : plan.Members)
                out << " [member=" << m.Guid << " role=" << RoleName(m.Role) << ']';
    }
    return out.str() + "; template readiness only; target dungeon access/locks unverified; nothing_queued";
}
}  // namespace PlayerbotAutonomy
