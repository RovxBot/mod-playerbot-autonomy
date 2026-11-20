#ifndef AUTONOMY_GROUP_ENGINE_FAKES_H
#define AUTONOMY_GROUP_ENGINE_FAKES_H
#include <cstdint>
#include <array>
#include <map>
#include <vector>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
enum class HighGuid
{
    Player,
    Group
};
struct ObjectGuid
{
    uint32 Id = 0;
    template <HighGuid> static ObjectGuid Create(uint32 id) { return {id}; }
    uint32 GetCounter() const { return Id; }
    bool IsEmpty() const { return !Id; }
};
enum Difficulty
{
    DUNGEON_DIFFICULTY_NORMAL,
    DUNGEON_DIFFICULTY_HEROIC,
    MAX_DIFFICULTY
};
inline constexpr Difficulty RAID_DIFFICULTY_10MAN_NORMAL = DUNGEON_DIFFICULTY_NORMAL;
class Group;
enum GroupHook
{
    GROUPHOOK_ON_ADD_MEMBER,
    GROUPHOOK_ON_REMOVE_MEMBER,
    GROUPHOOK_ON_CHANGE_LEADER,
    GROUPHOOK_ON_DISBAND
};
enum RemoveMethod : uint8
{
    GROUP_REMOVEMETHOD_DEFAULT
};
class GroupScript;
inline GroupScript* Observer = nullptr;
class GroupScript
{
public:
    GroupScript(char const*, std::vector<GroupHook>) { Observer = this; }
    virtual ~GroupScript() = default;
    virtual void OnAddMember(Group*, ObjectGuid) {}
    virtual void OnRemoveMember(Group*, ObjectGuid, RemoveMethod, ObjectGuid, char const*) {}
    virtual void OnChangeLeader(Group*, ObjectGuid, ObjectGuid) {}
    virtual void OnDisband(Group*) {}
};
struct Player;
struct WorldSession
{
    uint32 Account = 7;
    uint32 GetAccountId() const { return Account; }
};
struct PlayerbotAI
{
    bool HumanMaster = false;
    uint32 Gear = 180;
    bool HasGameClientMaster() const { return HumanMaster; }
    uint32 GetEquipGearScore(Player*) const { return Gear; }
};
struct Map
{
    enum EnterState
    {
        CAN_ENTER,
        DENIED
    };
    bool Dungeon = false;
    bool IsDungeon() const { return Dungeon; }
};
struct Player
{
    struct Item
    {
        bool Present = true, Broken = false;
        bool IsBroken() const { return Broken; }
    };
    std::array<Item, 19> Equipment;
    Item* GetItemByPos(uint8, uint8 slot) { return Equipment.at(slot).Present ? &Equipment.at(slot) : nullptr; }
    uint32 Id = 0, Guild = 23, Team = 0, Level = 80, MapId = 0, InstanceId = 0;
    bool Human = false, Self = false, Gm = false, HasAI = true, World = true, Alive = true;
    bool Combat = false, Teleport = false, Flight = false, Bg = false, Arena = false, Queue = false, Bound = false;
    Difficulty DungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL, RaidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
    Group* Party = nullptr;
    Group* Original = nullptr;
    Group* Invite = nullptr;
    WorldSession Session;
    PlayerbotAI AI;
    Map Location;
    ObjectGuid GetGUID() const { return {Id}; }
    WorldSession* GetSession() { return &Session; }
    uint32 GetGuildId() const { return Guild; }
    bool IsGameMaster() const { return Gm; }
    bool IsInWorld() const { return World; }
    bool IsAlive() const { return Alive; }
    bool IsInCombat() const { return Combat; }
    bool IsBeingTeleported() const { return Teleport; }
    bool IsInFlight() const { return Flight; }
    Map* GetMap() { return &Location; }
    Group* GetGroup() const { return Party; }
    Group* GetOriginalGroup() const { return Original; }
    Group* GetGroupInvite() const { return Invite; }
    bool InBattleground() const { return Bg; }
    bool InArena() const { return Arena; }
    bool InBattlegroundQueue() const { return Queue; }
    uint32 GetMapId() const { return MapId; }
    uint32 GetInstanceId() const { return InstanceId; }
    uint32 GetLevel() const { return Level; }
    uint32 GetTeamId() const { return Team; }
    Difficulty GetDungeonDifficulty() const { return DungeonDifficulty; }
    Difficulty GetRaidDifficulty() const { return RaidDifficulty; }
};
inline constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, EQUIPMENT_SLOT_MAINHAND = 15;
inline constexpr uint8 EQUIPMENT_SLOT_START = 0, EQUIPMENT_SLOT_END = 19;
inline std::map<uint32, Player> Players;
#define GET_PLAYERBOT_AI(p) ((p) && (p)->HasAI ? &(p)->AI : nullptr)
inline bool IsRealPlayer(Player* p) { return p->Human; }
inline bool IsSelfBot(Player* p) { return p->Self; }
namespace ObjectAccessor
{
inline Player* FindConnectedPlayer(ObjectGuid id)
{
    auto it = Players.find(id.Id);
    return it == Players.end() ? nullptr : &it->second;
}
} // namespace ObjectAccessor
struct CacheEntry
{
    uint32 AccountId = 7;
    ObjectGuid GroupGuid;
};
struct CharacterCache
{
    std::map<uint32, CacheEntry> Entries;
    CacheEntry* GetCharacterCacheByGuid(ObjectGuid id)
    {
        auto it = Entries.find(id.Id);
        return it == Entries.end() ? nullptr : &it->second;
    }
};
inline CharacterCache Cache;
#define sCharacterCache (&Cache)
struct GroupMgr
{
    std::map<uint32, Group*> Groups;
    Group* GetGroupByGUID(uint32 id)
    {
        auto it = Groups.find(id);
        return it == Groups.end() ? nullptr : it->second;
    }
    void AddGroup(Group* g);
};
inline GroupMgr Groups;
#define sGroupMgr (&Groups)
inline unsigned CreateCalls = 0, AddCalls = 0, DisbandCalls = 0;
class Group
{
public:
    struct Slot
    {
        ObjectGuid guid;
    };
    uint32 Id = 0, Leader = 0;
    bool Raid = false, Lfg = false, Bg = false;
    std::vector<Slot> Slots;
    bool Create(Player* p)
    {
        ++CreateCalls;
        Id = 991;
        Leader = p->Id;
        return AddMember(p);
    }
    bool AddMember(Player* p)
    {
        ++AddCalls;
        Slots.push_back({{p->Id}});
        p->Party = this;
        Cache.Entries[p->Id].GroupGuid = {Id};
        if (Observer)
            Observer->OnAddMember(this, p->GetGUID());
        return true;
    }
    ObjectGuid GetGUID() const { return {Id}; }
    ObjectGuid GetLeaderGUID() const { return {Leader}; }
    std::vector<Slot> const& GetMemberSlots() const { return Slots; }
    uint32 GetMembersCount() const { return Slots.size(); }
    bool isRaidGroup() const { return Raid; }
    bool isLFGGroup() const { return Lfg; }
    bool isBGGroup() const { return Bg; }
    Difficulty GetDungeonDifficulty() const { return DUNGEON_DIFFICULTY_NORMAL; }
    void Disband()
    {
        ++DisbandCalls;
        if (Observer)
            Observer->OnDisband(this);
        for (auto s : Slots)
        {
            Players.at(s.guid.Id).Party = nullptr;
            Cache.Entries[s.guid.Id].GroupGuid = {};
        }
        Groups.Groups.erase(Id);
        delete this;
    }
};
inline void GroupMgr::AddGroup(Group* g) { Groups[g->Id] = g; }
struct InstanceSaveMgr
{
    std::vector<int> PlayerGetBoundInstances(ObjectGuid id, Difficulty)
    {
        return Players.at(id.Id).Bound ? std::vector<int>{1} : std::vector<int>{};
    }
};
inline InstanceSaveMgr Saves;
#define sInstanceSaveMgr (&Saves)
namespace lfg
{
inline constexpr unsigned LFG_STATE_NONE = 0;
}
struct LfgMgr
{
    unsigned GetState(ObjectGuid id) { return Players.at(id.Id).Queue ? 1 : 0; }
};
inline LfgMgr Lfg;
#define sLFGMgr (&Lfg)
struct SessionMgr
{
    bool Human = false;
    WorldSession S;
    WorldSession* FindSession(uint32) { return Human ? &S : nullptr; }
};
inline SessionMgr Sessions;
#define sWorldSessionMgr (&Sessions)
struct RandomMgr
{
    bool Dedicated = true;
    bool IsDedicatedRandomAccount(uint32) const { return Dedicated; }
    Player* GetPlayerBot(ObjectGuid id) { return ObjectAccessor::FindConnectedPlayer(id); }
};
inline RandomMgr Random;
#define sRandomPlayerbotMgr Random
struct Sidecar
{
    bool Cluster = false;
    bool ClusterModeEnabled() const { return Cluster; }
};
inline Sidecar Cloud;
#define sToCloud9Sidecar (&Cloud)
struct MapManager
{
    bool Allowed = true;
    uint32 LastMap = 0;
    Map::EnterState PlayerCannotEnter(uint32 map, Player*, bool)
    {
        LastMap = map;
        return Allowed && map == 389 ? Map::CAN_ENTER : Map::DENIED;
    }
};
inline MapManager Maps;
#define sMapMgr (&Maps)
namespace PlayerbotAutonomy
{
enum class EnrollmentState
{
    Unknown,
    Persistent
};
struct Record
{
    bool IdentityValid = true, AccountIsRandomBot = true;
    EnrollmentState State = EnrollmentState::Persistent;
    uint32 Account = 7, Guild = 23;
};
struct EnrollmentCache
{
    Record Row;
    uint64 Owner = 42;
    Record Get(uint32) const { return Row; }
    uint64 OperationOwner(uint32) const { return Owner; }
};
struct FakeRuntime
{
    EnrollmentCache Records;
    EnrollmentCache& Cache() { return Records; }
};
inline FakeRuntime RuntimeInstance;
inline FakeRuntime& Runtime() { return RuntimeInstance; }
} // namespace PlayerbotAutonomy
#endif
