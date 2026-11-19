#include "RealGroupExecutor.h"

#if __has_include("Group.h") && __has_include("TC9Sidecar.h")
#include "CharacterCache.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupScript.h"
#include "InstanceSaveMgr.h"
#include "LFGMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Persistence/EnrollmentRuntime.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "TC9Sidecar.h"
#include "WorldSessionMgr.h"
#include <atomic>
#include <mutex>

namespace PlayerbotAutonomy
{
namespace
{
Player* Find(uint32 guid) { return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid)); }
bool Controlled(Player* p)
{
    auto* ai = p ? GET_PLAYERBOT_AI(p) : nullptr;
    if (!p || !ai || !p->GetSession() || p->IsGameMaster() || IsRealPlayer(p) || IsSelfBot(p) ||
        ai->HasGameClientMaster() || sWorldSessionMgr->FindSession(p->GetSession()->GetAccountId()) ||
        !sRandomPlayerbotMgr.IsDedicatedRandomAccount(p->GetSession()->GetAccountId()) ||
        sRandomPlayerbotMgr.GetPlayerBot(p->GetGUID()) != p)
        return false;
    auto r = Runtime().Cache().Get(p->GetGUID().GetCounter());
    return r.IdentityValid && r.State == EnrollmentState::Persistent && r.AccountIsRandomBot &&
           r.Account == p->GetSession()->GetAccountId() && r.Guild == p->GetGuildId();
}
bool Bound(Player* p)
{
    for (uint8 d = 0; d < MAX_DIFFICULTY; ++d)
        if (!sInstanceSaveMgr->PlayerGetBoundInstances(p->GetGUID(), Difficulty(d)).empty())
            return true;
    return false;
}
class CoreGroupPort final : public GroupPort
{
public:
    void Changed(uint32 id, uint32 member = 0)
    {
        if (!_trackedCount.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _tracked.find(id);
        if (it != _tracked.end() && !(id == _addingGroup && member && member == _addingMember))
            it->second = false;
    }
    void Gone(uint32 id)
    {
        if (!_trackedCount.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> lock(_mutex);
        _tracked.erase(id);
        _trackedCount.store(_tracked.size(), std::memory_order_release);
    }
    std::string Admission(Operation const& op) const override
    {
        if (sToCloud9Sidecar->ClusterModeEnabled())
            return "cluster_group_lifecycle_unsupported";
        unsigned tanks = 0, heals = 0, dps = 0;
        std::set<uint32> guids;
        for (auto const& m : op.Members)
        {
            auto* p = Find(m.Guid);
            if (!Controlled(p) || !guids.insert(m.Guid).second || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() ||
                p->IsBeingTeleported() || p->IsInFlight() || !p->GetMap() || p->GetMap()->IsDungeon() ||
                p->GetGroup() || p->GetOriginalGroup() || p->GetGroupInvite() || p->InBattleground() || p->InArena() ||
                p->InBattlegroundQueue() || sLFGMgr->GetState(p->GetGUID()) != lfg::LFG_STATE_NONE ||
                Runtime().Cache().OperationOwner(m.Guid) != op.Id)
                return "assembly_live_identity_or_activity_conflict";
            if (Bound(p))
                return "existing_instance_binding_group_join_would_reset";
            if (p->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL ||
                p->GetRaidDifficulty() != RAID_DIFFICULTY_10MAN_NORMAL)
                return "existing_difficulty_would_be_changed";
            // Target-specific admission, followed by the actual core access
            // policy. These conservative equipment/level floors do not assert
            // that the party can clear an encounter.
            if (p->GetLevel() < 70 || GET_PLAYERBOT_AI(p)->GetEquipGearScore(p) < 115)
                return "utgarde_keep_capability_floor_not_met";
            if (sMapMgr->PlayerCannotEnter(574, p, true) != Map::CAN_ENTER)
                return "utgarde_keep_access_denied";
            if (p->GetTeamId() != op.Faction || p->GetGuildId() != m.Guild || m.Guild != op.Guild ||
                p->GetSession()->GetAccountId() != m.Account)
                return "assembly_identity_changed";
            tanks += m.Role == CombatRole::Tank;
            heals += m.Role == CombatRole::Healer;
            dps += m.Role == CombatRole::Dps;
            if (m.Guid == op.Leader && m.Role != CombatRole::Tank)
                return "leader_not_selected_tank";
        }
        return guids.size() == 5 && guids.count(op.Leader) && tanks == 1 && heals == 1 && dps == 3
                   ? ""
                   : "invalid_five_member_roles";
    }
    GroupEvidence Inspect(uint32 id) const override
    {
        GroupEvidence e;
        auto* group = id ? sGroupMgr->GetGroupByGUID(id) : nullptr;
        if (!group)
            return e;
        e.Id = id;
        e.Leader = group->GetLeaderGUID().GetCounter();
        e.Normal = !sToCloud9Sidecar->ClusterModeEnabled() && !group->isRaidGroup() && !group->isLFGGroup() &&
                   !group->isBGGroup() && group->GetDungeonDifficulty() == DUNGEON_DIFFICULTY_NORMAL;
        e.SafeControl = e.SafeToDisband = true;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto tracked = _tracked.find(id);
            e.SafeControl = tracked != _tracked.end() && tracked->second;
        }
        bool first = true, colocated = true;
        for (auto const& slot : group->GetMemberSlots())
        {
            auto guid = slot.guid.GetCounter();
            e.Members.push_back(guid);
            auto* p = Find(guid);
            e.SafeControl = e.SafeControl && Controlled(p) && p->GetGroup() == group && !p->GetOriginalGroup() &&
                            p->IsInWorld() && p->IsAlive() && !p->IsBeingTeleported() && !p->IsInFlight() &&
                            !p->InBattlegroundQueue() && sLFGMgr->GetState(p->GetGUID()) == lfg::LFG_STATE_NONE;
            if (first && p)
            {
                e.Map = p->GetMapId();
                e.Instance = p->GetInstanceId();
                first = false;
            }
            if (!p || p->GetMapId() != e.Map || p->GetInstanceId() != e.Instance)
                colocated = false;
            e.SafeToDisband = e.SafeToDisband && p && p->IsInWorld() && p->IsAlive() && !p->IsInCombat() &&
                              !p->IsBeingTeleported() && p->GetMap() && !p->GetMap()->IsDungeon() && !Bound(p);
        }
        if (!colocated)
            e.Map = e.Instance = 0;
        return e;
    }
    uint32 Create(uint32 leader) override
    {
        auto* p = Find(leader);
        if (!Controlled(p) || p->GetGroup() || sToCloud9Sidecar->ClusterModeEnabled())
            return 0;
        auto* group = new Group();
        if (!group->Create(p))
        {
            delete group;
            return 0;
        }
        sGroupMgr->AddGroup(group);
        auto id = group->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tracked[id] = true;
            _trackedCount.store(_tracked.size(), std::memory_order_release);
        }
        return id;
    }
    bool Add(Operation const& op, RosterMember const& m) override
    {
        auto id = op.Group;
        auto member = m.Guid;
        auto* group = sGroupMgr->GetGroupByGUID(id);
        auto* p = Find(member);
        if (!group || group->GetMembersCount() >= 5 || !Controlled(p) || p->GetGroup() || p->GetOriginalGroup() ||
            p->GetGroupInvite() || Bound(p) || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() ||
            p->IsBeingTeleported() || p->IsInFlight() || !p->GetMap() || p->GetMap()->IsDungeon() ||
            p->InBattleground() || p->InArena() || p->InBattlegroundQueue() ||
            sLFGMgr->GetState(p->GetGUID()) != lfg::LFG_STATE_NONE ||
            p->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL ||
            p->GetRaidDifficulty() != RAID_DIFFICULTY_10MAN_NORMAL || p->GetTeamId() != op.Faction ||
            p->GetSession()->GetAccountId() != m.Account || p->GetGuildId() != m.Guild ||
            Runtime().Cache().OperationOwner(member) != op.Id)
            return false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _addingGroup = id;
            _addingMember = member;
        }
        bool added = group->AddMember(p);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _addingGroup = _addingMember = 0;
        }
        return added;
    }
    bool Disband(uint32 id) override
    {
        auto e = Inspect(id);
        if (!e.Id || !e.Normal || !e.SafeControl || !e.SafeToDisband)
            return false;
        sGroupMgr->GetGroupByGUID(id)->Disband();
        return !sGroupMgr->GetGroupByGUID(id);
    }
    bool ParticipantsUngrouped(Operation const& op) const override
    {
        for (auto const& m : op.Members)
        {
            auto* c = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(m.Guid));
            if (!c || c->AccountId != m.Account || !c->GroupGuid.IsEmpty())
                return false;
            auto* p = Find(m.Guid);
            if (p && (p->GetGroup() || p->GetOriginalGroup()))
                return false;
        }
        return true;
    }

private:
    mutable std::mutex _mutex;
    std::map<uint32, bool> _tracked;
    std::atomic<std::size_t> _trackedCount{0};
    uint32 _addingGroup = 0, _addingMember = 0;
} port;
class OwnershipObserver final : public GroupScript
{
public:
    OwnershipObserver()
        : GroupScript("AutonomyGroupOwnership", {GROUPHOOK_ON_ADD_MEMBER, GROUPHOOK_ON_REMOVE_MEMBER,
                                                 GROUPHOOK_ON_CHANGE_LEADER, GROUPHOOK_ON_DISBAND})
    {
    }
    void OnAddMember(Group* g, ObjectGuid member) override
    {
        port.Changed(g->GetGUID().GetCounter(), member.GetCounter());
    }
    void OnRemoveMember(Group* g, ObjectGuid, RemoveMethod, ObjectGuid, char const*) override
    {
        port.Changed(g->GetGUID().GetCounter());
    }
    void OnChangeLeader(Group* g, ObjectGuid, ObjectGuid) override { port.Changed(g->GetGUID().GetCounter()); }
    void OnDisband(Group* g) override { port.Gone(g->GetGUID().GetCounter()); }
};
} // namespace
GroupPort& CoreGroups() { return port; }
void RegisterCoreGroupObserver() { new OwnershipObserver(); }
} // namespace PlayerbotAutonomy
#else
// A missing core capability is an unavailable adapter, never simulated success.
namespace PlayerbotAutonomy
{
namespace
{
class MissingGroupPort final : public GroupPort
{
public:
    bool Available() const override { return false; }
    std::string Admission(Operation const&) const override { return "core_group_adapter_unavailable"; }
    GroupEvidence Inspect(std::uint32_t) const override { return {}; }
    std::uint32_t Create(std::uint32_t) override { return 0; }
    bool Add(Operation const&, RosterMember const&) override { return false; }
    bool Disband(std::uint32_t) override { return false; }
    bool ParticipantsUngrouped(Operation const&) const override { return false; }
} port;
} // namespace
GroupPort& CoreGroups() { return port; }
void RegisterCoreGroupObserver() {}
} // namespace PlayerbotAutonomy
#endif
