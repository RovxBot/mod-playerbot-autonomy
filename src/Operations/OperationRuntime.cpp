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
#if AUTONOMY_DC_BRIDGE
namespace
{
std::atomic<OperationRuntime*> BridgeOwnerRuntime{nullptr};
bool AuthorizeBridge(DcAutonomy::Permit const& p, bool consume)
{
    auto* runtime = BridgeOwnerRuntime.load(std::memory_order_acquire);
    return runtime && runtime->Authorize(p, consume);
}
DcAutonomy::Owner const BridgeOwner{DcAutonomy::Version, AuthorizeBridge};
}
#endif
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
        _decision = "invalid_configuration";
    }
    if (settings.AssembleGroups && !Groups().Available())
    {
        _settings.Enabled = false;
        _decision = "core_group_adapter_unavailable";
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
#if AUTONOMY_DC_BRIDGE
    _providerIdentity = _ids();
    if (!_providerIdentity) _providerIdentity = 1;
    OperationRuntime* expected = nullptr;
    if (!(BridgeOwnerRuntime.compare_exchange_strong(expected, this) || expected == this) ||
        !DcAutonomy::RegisterOwner(&BridgeOwner)) _decision = "dungeon_owner_provider_conflict";
#endif
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
    auto rules = _settings.Rules;
    if (_settings.AssembleGroups)
    {
        rules.MinLevel = std::max(rules.MinLevel, 70u);
        rules.MinItemLevel = std::max(rules.MinItemLevel, 115u);
    }
    return PlanParty(candidates, rules, guild, faction, _now);
}
bool OperationRuntime::ManagedGuilds(Operation const& operation, bool stopping) const
{
    if (!OnWorldThread() || !Runtime().ProtectionAvailable() || !Runtime().Cache().Ready() ||
        (!stopping && !Guilds().Ready()))
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
    return true;
}
bool OperationRuntime::Validate(Operation const& operation, bool requireLease) const
{
    if (!ManagedGuilds(operation)) return false;
    uint32 low = 80, high = 0;
    bool owned = false;
    if (operation.Execution == ExecutionMode::RealGroup && operation.Group)
    {
        owned = RealGroupExecutor::Owned(operation, Groups().Inspect(operation.Group));
        if (!owned) return false;
    }
    for (auto const& member : operation.Members)
    {
        auto m = Observe(member.Guid, member.Guild, true);
        if (owned)
        {
            // The group adapter has verified every live session, exact member
            // slot, M1 identity, queues and control. Recruitment's deliberately
            // ungrouped Candidate() is not an availability policy for our group.
            m.Grouped = m.InInstance = false;
            m.Online = m.Available = true;
        }
        if (operation.Execution == ExecutionMode::RealGroup && (m.Level < 70 || m.EquippedItemLevel < 115))
            return false;
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
            if (_pending && !_before && !SameOperation(durable, *_pending))
            {
                // No core action is possible before proposal readback. Reject
                // the collision without adopting or cancelling the other intent.
                Runtime().Cache().ReleaseOperation(id);
                _pending.reset();
                ++_conflicts;
                _cooldownUntil = _now + _settings.CooldownSeconds;
                _decision = "proposal_id_collision_rejected";
                return;
            }
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
                _createdGroups.erase(id);
                _groupProofReady.erase(id);
                _groupAbsenceReady.erase(id);
                _restartGroups.erase(id);
                _operations.erase(id);
                _decision = durable.Reason;
                if (_recoverId == id)
                    _recoverId = 0;
            }
            else
            {
                _operations[id] = std::move(durable);
                auto const& op = _operations[id];
                if (op.Execution == ExecutionMode::RealGroup && expectedLeases &&
                    !Runtime().Cache().AcquireOperation(id, ParticipantGuids(op)))
                {
                    _decision = "real_operation_recovery_waiting_m1_or_lease_conflict";
                    _recoverId = id;
                }
                if (!_recovered)
                {
                    _recoverId = id;
                    if (op.Execution == ExecutionMode::RealGroup) _restartGroups.insert(id);
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
void OperationRuntime::ReadGroup(Operation const& op, bool absence)
{
    ++_queries;
    _age = 0;
    _query.emplace((absence ? _store.GroupAbsent(op) : _store.GroupSnapshot(op))
                       .WithPreparedCallback(
                           [this, op, absence](PreparedQueryResult result)
                           {
                               if (!result || !_operations.count(op.Id) || _operations.at(op.Id).Group != op.Group)
                               {
                                   ++_failures;
                                   _retryIn = 5000;
                                   return;
                               }
                               if (absence)
                               {
                                   if (result->Fetch()[0].Get<uint64>() == 1 && Groups().ParticipantsUngrouped(op))
                                       _groupAbsenceReady.insert(op.Id);
                                   else
                                   {
                                       _decision = "cleanup_waiting_core_group_sql_delete";
                                       _retryIn = 5000;
                                   }
                                   return;
                               }
                               auto live = Groups().Inspect(op.Group);
                               std::set<uint32> committed, authoritative(live.Members.begin(), live.Members.end());
                               bool valid = RealGroupExecutor::Owned(op, live, true);
                               do
                               {
                                   auto* row = result->Fetch();
                                   if (row[0].IsNull() || row[1].IsNull() || row[2].IsNull() ||
                                       row[0].Get<uint32>() != op.Leader || row[2].Get<uint32>() != op.Group)
                                   {
                                       valid = false;
                                       break;
                                   }
                                   valid = valid && committed.insert(row[1].Get<uint32>()).second;
                               } while (result->NextRow());
                               if (valid && committed == authoritative)
                                   _groupProofReady.insert(op.Id);
                               else
                               {
                                   _decision = _now >= op.Deadline ? "manual_core_group_sql_creation_unconfirmed"
                                                                   : "waiting_core_group_sql_creation";
                                   _retryIn = 5000;
                               }
                           }));
}
#if AUTONOMY_DC_BRIDGE
bool OperationRuntime::Authorize(DcAutonomy::Permit const& p, bool consume)
{
    if (!OnWorldThread() || !_permit || !DcAutonomy::Same(p, *_permit) || p.owner != _providerIdentity ||
        !_settings.Enabled || _settings.DryRun || !_settings.AssembleGroups || !_settings.TrustedDungeon ||
        _settings.KillSwitch || !_operations.count(p.operation))
        return false;
    auto const& op = _operations.at(p.operation);
    if (op.Execution != ExecutionMode::RealGroup || !op.GroupConfirmed || op.Group != p.group ||
        op.Leader != p.leader || Terminal(op.State) || op.State == OperationState::Closing ||
        op.State == OperationState::Recovering || _now >= op.Deadline || _now >= op.LeaseUntil || !Validate(op))
        return false;
    auto guids = ParticipantGuids(op);
    if (!std::equal(guids.begin(), guids.end(), p.members.begin()))
        return false;
    if (!_permitConsumed && (p.revision != op.Revision || p.expires <= _now))
        return false;
    if (consume)
    {
        if (_permitConsumed)
            return false;
        _permitConsumed = true;
    }
    return true;
}
#endif
Operation OperationRuntime::CloseReal(Operation const& current)
{
    Operation next = current;
    // Restore local ownership before examining/changing anything after restart.
    if (!Runtime().Cache().AcquireOperation(current.Id, ParticipantGuids(current)))
    {
        _decision = "recovery_waiting_for_m1_or_lease_conflict";
        return next;
    }
    if (!current.Group && _createdGroups.count(current.Id))
    {
        next.Group = _createdGroups.at(current.Id);
        next.State = OperationState::Closing;
        next.Reason = "reconcile_local_core_group_receipt";
        return next;
    }
#if AUTONOMY_DC_BRIDGE
    if (_permit && _permit->operation == current.Id && _permitConsumed)
    {
        auto* bridge = DcAutonomy::GetExecutor();
        if (bridge)
            _lastDungeonStatus = bridge->stop(*_permit);
        if (!bridge || !_lastDungeonStatus.quiescent)
        {
            _decision = "cleanup_pending_dungeon_executor_stop";
            return next;
        }
    }
#endif
    auto live = Groups().Inspect(current.Group);
    if (_restartGroups.count(current.Id) && current.Group && live.Id)
    {
        _decision = "manual_restart_group_ownership_reconciliation";
        return next;
    }
    if (live.Id && !ManagedGuilds(current, true))
    {
        _decision = "manual_cleanup_guild_ownership_changed_or_unknown";
        return next;
    }
    if (current.Group && !current.GroupConfirmed && !_restartGroups.count(current.Id))
    {
        if (_groupProofReady.erase(current.Id))
        {
            next.GroupConfirmed = true;
            next.State = OperationState::Closing;
            next.Reason = "durable_core_group_confirmed_for_cleanup";
        }
        else if (live.Id)
            ReadGroup(current, false);
        else
            _decision = "manual_core_creation_not_confirmed_before_group_loss";
        return next;
    }
    ++_groupCalls;
    auto cleaned = RealGroupExecutor(Groups()).Cleanup(current);
    _decision = cleaned.Reason;
    if (cleaned.Clean)
    {
        if (_groupAbsenceReady.erase(current.Id))
        {
            next.State = current.Succeeded ? OperationState::Completed : OperationState::Cancelled;
            next.Reason = cleaned.Reason;
            _cooldownUntil = _now + _settings.CooldownSeconds;
        }
        else
            ReadGroup(current, true);
    }
    else if (current.State != OperationState::Closing)
    {
        next.State = OperationState::Closing;
        next.Reason = cleaned.Reason;
    }
    return next;
}
Operation OperationRuntime::AdvanceReal(Operation const& current, bool recovering)
{
    Operation next = current;
    auto change = [&](OperationState state, std::string reason)
    {
        next.State = state;
        if (state == OperationState::Closing)
        {
            _lastRealOperation = current.Id;
            _lastRealReason = reason;
        }
        next.Reason = std::move(reason);
    };
    bool enabled = _settings.Enabled && !_settings.DryRun && _settings.AssembleGroups && !_settings.KillSwitch;
    bool closing =
        recovering || current.State == OperationState::Closing || current.State == OperationState::Recovering;
    if (!closing && (!enabled || _now >= current.Deadline || _now >= current.LeaseUntil))
        change(OperationState::Closing, enabled ? "real_operation_timeout" : "real_feature_disabled");
    else if (closing)
        next = CloseReal(current);
    else if (!Validate(current))
        change(OperationState::Closing, "real_participant_or_group_changed");
    else if (current.State == OperationState::Proposed)
        change(OperationState::Validated, "real_participants_validated");
    else if (current.State == OperationState::Validated)
        change(OperationState::Reserved, "real_durable_reservations_requested");
    else if (current.State == OperationState::Reserved)
    {
        next.CreationFenced = true;
        _creationAllowed = current.Id; // Usable only after matching SQL readback.
        change(OperationState::Assembling, "real_creation_fenced");
    }
    else if (current.State == OperationState::Assembling)
    {
        if (_now >= current.CreatedAt + _settings.AssemblySeconds)
            change(OperationState::Closing, "real_assembly_or_colocation_timeout");
        else if (!current.Group)
        {
            bool permission = _creationAllowed == current.Id;
            _creationAllowed = 0; // Burn before core API, including partial failures.
            ++_groupCalls;
            auto result = RealGroupExecutor(Groups()).Assemble(current, permission);
            next.Group = result.Group;
            if (result.Group)
                _createdGroups[current.Id] = result.Group;
            change(result.Ready ? OperationState::Assembling : OperationState::Closing, result.Reason);
        }
        else
        {
            auto group = Groups().Inspect(current.Group);
            if (!current.GroupConfirmed)
            {
                if (_groupProofReady.erase(current.Id))
                {
                    next.GroupConfirmed = true;
                    change(OperationState::Assembling, "durable_core_group_confirmed");
                }
                else
                {
                    ReadGroup(current, false);
                    return next;
                }
            }
            else
            {
                _decision = "real_group_ready_waiting_eligible_colocation";
#if AUTONOMY_DC_BRIDGE
                auto* bridge = DcAutonomy::GetExecutor();
                if (!_settings.TrustedDungeon)
                    _decision = "trusted_dungeon_disabled_group_ready";
                else if (!bridge)
                    _decision = "dungeon_adapter_unavailable";
                else if (group.Map == 574 && group.Instance)
                {
                    if (!_permit || _permit->operation != current.Id || (!_permitConsumed && _now >= _permit->expires))
                    {
                        DcAutonomy::Permit p;
                        p.owner = _providerIdentity;
                        p.operation = current.Id;
                        p.revision = current.Revision;
                        p.nonce = _ids();
                        if (!p.nonce)
                            p.nonce = 1;
                        p.expires = _now + 10;
                        p.group = current.Group;
                        p.leader = current.Leader;
                        p.map = 574;
                        p.instance = group.Instance;
                        auto guids = ParticipantGuids(current);
                        std::copy(guids.begin(), guids.end(), p.members.begin());
                        _permit = p;
                        _permitConsumed = false;
                    }
                    // An accepted start is never retried. A delayed SQL ack cannot
                    // create another run or consume a second permit.
                    auto status = _permitConsumed ? bridge->status(*_permit) : bridge->start(*_permit);
                    _lastDungeonStatus = status;
                    _decision = status.reason;
                    if (status.state == DcAutonomy::State::Running || status.state == DcAutonomy::State::Paused)
                        change(OperationState::Active, "trusted_dungeon_start_acknowledged");
                    else if (_permitConsumed)
                        change(OperationState::Closing, "trusted_dungeon_start_failed");
                }
#else
                (void)group;
                if (_settings.TrustedDungeon)
                    _decision = "dungeon_adapter_unavailable";
#endif
            }
        }
    }
    else if (current.State == OperationState::Active)
    {
#if AUTONOMY_DC_BRIDGE
        auto* bridge = DcAutonomy::GetExecutor();
        if (!_settings.TrustedDungeon || !bridge || !_permit || _permit->operation != current.Id)
            change(OperationState::Closing, "dungeon_adapter_or_feature_unavailable");
        else
        {
            auto status = bridge->status(*_permit);
            _lastDungeonStatus = status;
            _decision = status.reason;
            if (DcAutonomy::Terminal(status.state))
            {
                next.Succeeded = status.state == DcAutonomy::State::Completed;
                change(OperationState::Closing, status.reason);
            }
            else if (!status.lastProgress ||
                     (_now >= status.lastProgress && _now - status.lastProgress >= _settings.ProgressSeconds))
                change(OperationState::Closing, "dungeon_no_progress_timeout");
        }
#else
        change(OperationState::Closing, "dungeon_adapter_unavailable");
#endif
    }
    else
        change(OperationState::Closing, "unsupported_real_operation_state");
    if (!SameOperation(current, next))
    {
        ++next.Revision;
        ++next.Attempts;
        next.ChangedAt = _now;
    }
    return next;
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
        if (item.second.Execution == ExecutionMode::Simulated && _now >= item.second.LeaseUntil)
            Runtime().Cache().ReleaseOperation(item.first);
    if (_pending && _pending->Execution == ExecutionMode::Simulated && _now >= _pending->LeaseUntil)
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
        if (current.Execution == ExecutionMode::RealGroup)
        {
            next = AdvanceReal(current, true);
            if (!SameOperation(current, next)) Save(std::move(next), &current);
            else _retryIn = 5000;
        }
        else if (current.State != OperationState::Recovering)
        {
            next.State = OperationState::Recovering;
            ++next.Revision;
            ++next.Attempts;
            next.ChangedAt = _now;
            next.Reason = "interrupted_simulation";
        }
        else
            next = NextOperation(current, _now, false, false);
        if (current.Execution == ExecutionMode::Simulated) Save(std::move(next), &current);
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
            auto next = current.Execution == ExecutionMode::RealGroup ? AdvanceReal(current) :
                NextOperation(current, _now, _settings.Enabled && !_settings.DryRun, Validate(current));
            if (!SameOperation(current, next)) Save(std::move(next), &current);
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
                if (plan.Complete() && !_settings.DryRun && !_settings.KillSwitch && _now >= _cooldownUntil &&
                    _operations.size() < _settings.MaxConcurrent &&
                    (!_settings.AssembleGroups || std::none_of(_operations.begin(), _operations.end(),
                        [](auto const& item) { return item.second.Execution == ExecutionMode::RealGroup; })))
                {
                    Operation op;
                    do
                    {
                        op.Id = _ids() & 0x7fffffffffffffffULL;
                    } while (!op.Id || _operations.count(op.Id));
                    op.Guild = plan.Guild;
                    op.Faction = plan.Faction;
                    op.Leader = plan.Leader;
                    op.Members = std::move(plan.Members);
                    if (_settings.AssembleGroups)
                    {
                        op.Execution = ExecutionMode::RealGroup;
                        op.Reason = "real_group_proposed";
                    }
                    op.CreatedAt = op.ChangedAt = _now;
                    op.Deadline = _now + (op.Execution == ExecutionMode::RealGroup ?
                        _settings.RunSeconds + _settings.AssemblySeconds : _settings.TimeoutSeconds);
                    op.LeaseUntil = op.Deadline + (op.Execution == ExecutionMode::RealGroup ?
                        60 : _settings.LeaseSeconds - _settings.TimeoutSeconds);
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
#if AUTONOMY_DC_BRIDGE
    OperationRuntime* expected = this;
    BridgeOwnerRuntime.compare_exchange_strong(expected, nullptr);
    _permit.reset();
    _permitConsumed = false;
#endif
    _creationAllowed = 0;
    _restartGroups.clear();
    _groupProofReady.clear();
    _groupAbsenceReady.clear();
    _createdGroups.clear();
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
    out << "operations enabled=" << _settings.Enabled << " dry_run=" << _settings.DryRun
        << " assembly=" << (_settings.AssembleGroups ? "real" : "simulated")
        << " store=" << _opened << " recovered=" << _recovered << " snapshots=" << _roster.size()
        << " operations=" << _operations.size() << " pending_write=" << bool(_pending) << " queries=" << _queries
        << " writes=" << _writes << " failures=" << _failures << " lease_conflicts=" << _conflicts
        << " group_checks=" << _groupCalls << " real_gate=" << _settings.AssembleGroups
        << " trusted_gate=" << _settings.TrustedDungeon << " kill_switch=" << _settings.KillSwitch
        << " last_real_id=" << _lastRealOperation << " last_real_reason=" << _lastRealReason
        << " recoveries=" << _recoveries << " ticks=" << _ticks << " last_tick_us=" << _lastTickUs
        << " max_tick_us=" << _maxTickUs << " decision=" << _decision;
    uint32 reserved = 0;
    for (auto const& item : _operations)
    {
        auto const& op = item.second;
        for (auto guid : ParticipantGuids(op))
            reserved += Runtime().Cache().OperationOwner(guid) == op.Id;
        out << " [id=" << op.Id << " guild=" << op.Guild << " state=" << OperationStateName(op.State)
            << " mode=" << (op.Execution == ExecutionMode::RealGroup ? "REAL" : "SIMULATED")
            << " group=" << op.Group << " creation_fenced=" << op.CreationFenced << " group_sql_confirmed=" << op.GroupConfirmed
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
#if AUTONOMY_DC_BRIDGE
    out << " dc_state=" << DcAutonomy::StateName(_lastDungeonStatus.state)
        << " dc_operation=" << _lastDungeonStatus.operation << " dc_run=" << _lastDungeonStatus.run
        << " dc_map=" << _lastDungeonStatus.map << " dc_instance=" << _lastDungeonStatus.instance
        << " dc_quiescent=" << _lastDungeonStatus.quiescent << " dc_reason=" << _lastDungeonStatus.reason;
#endif
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
