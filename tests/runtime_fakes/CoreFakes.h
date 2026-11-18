#ifndef AUTONOMY_TEST_CORE_FAKES_H
#define AUTONOMY_TEST_CORE_FAKES_H
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
using uint8 = std::uint8_t;
using int32 = std::int32_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
inline bool FakeDatabaseReady = true;
inline unsigned FakeDatabaseCalls = 0;
#ifdef AUTONOMY_PREPARED_MYSQL
#include "PreparedMySQLAdapter.h"
#else
class Field
{
public:
    using Value = std::variant<std::nullptr_t, uint64, std::string>;
    Value Data = nullptr;
    Field() = default;
    Field(uint64 value) : Data(value) {}
    Field(std::string value) : Data(std::move(value)) {}
    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(Data); }
    template <class T>
    T Get() const
    {
        if (IsNull())
            return T{};
        if constexpr (std::is_same_v<T, std::string>)
            return std::get<std::string>(Data);
        else
            return static_cast<T>(std::get<uint64>(Data));
    }
};
class PreparedResultSet
{
public:
    std::vector<std::vector<Field> > Rows;
    std::size_t Index = 0;
    Field* Fetch() { return Rows.at(Index).data(); }
    bool NextRow() { return ++Index < Rows.size(); }
};
using PreparedQueryResult = std::shared_ptr<PreparedResultSet>;
class QueryCallback
{
public:
    explicit QueryCallback(PreparedQueryResult result) : _result(std::move(result)) {}
    QueryCallback&& WithPreparedCallback(std::function<void(PreparedQueryResult)> callback)
    {
        _callback = std::move(callback);
        return std::move(*this);
    }
    bool InvokeIfReady()
    {
        if (!FakeDatabaseReady)
            return false;
        _callback(_result);
        return true;
    }

private:
    PreparedQueryResult _result;
    std::function<void(PreparedQueryResult)> _callback;
};
class TransactionCallback
{
public:
    explicit TransactionCallback(std::function<bool()> work) : _work(std::move(work)) {}
    void AfterComplete(std::function<void(bool)> callback) { _callback = std::move(callback); }
    bool InvokeIfReady()
    {
        if (!FakeDatabaseReady)
            return false;
        _callback(_work());
        return true;
    }

private:
    std::function<bool()> _work;
    std::function<void(bool)> _callback;
};
struct MySQLConnectionInfo
{
};
class SQLOperation;
template <class T>
class ProducerConsumerQueue;
class MySQLConnection
{
public:
    explicit MySQLConnection(MySQLConnectionInfo&) {}
    MySQLConnection(ProducerConsumerQueue<SQLOperation*>*, MySQLConnectionInfo&) {}
    virtual ~MySQLConnection() = default;
    virtual void DoPrepareStatements() = 0;
};
class ModuleDatabasePool
{
public:
    virtual ~ModuleDatabasePool() = default;
    void Close() {}

protected:
    virtual MySQLConnection* CreateConnection(MySQLConnectionInfo&) = 0;
    virtual MySQLConnection* CreateAsyncConnection(ProducerConsumerQueue<SQLOperation*>*, MySQLConnectionInfo&) = 0;
};
template <class T>
class PreparedStatement;
#endif
enum class HighGuid
{
    Player
};
struct ObjectGuid
{
    using LowType = uint32;
    uint32 Value = 0;
    template <HighGuid>
    static ObjectGuid Create(uint32 guid)
    {
        return {guid};
    }
    uint32 GetCounter() const { return Value; }
    uint64 GetRawValue() const { return Value; }
    bool IsEmpty() const { return Value == 0; }
};
struct WorldSession
{
    uint32 Account = 7;
    uint32 GetAccountId() const { return Account; }
};
struct PlayerbotAI
{
    bool HumanMaster = false;
    bool HasGameClientMaster() const { return HumanMaster; }
};
struct Player
{
    uint32 Guid = 42;
    uint32 Guild = 0;
    bool Grouped = false;
    bool OriginalGrouped = false;
    bool Online = true;
    bool Selfbot = false;
    bool Human = false;
    bool HasSession = true;
    bool HasAI = true;
    bool Combat = false;
    bool Teleporting = false;
    bool Alive = true;
    bool InFlight = false, InWorld = true, Dungeon = false;
    uint32 Invite = 0;
    uint32 GetGuildIdInvited() const { return Invite; }
    bool IsInFlight() const { return InFlight; }
    bool InBattleground() const { return false; }
    bool InArena() const { return false; }
    bool InBattlegroundQueue() const { return false; }
    bool IsInWorld() const { return InWorld; }
    struct MapView
    {
        bool IsDungeon() const { return FakeDungeon; }
        bool FakeDungeon = false;
    } Map;
    MapView* GetMap()
    {
        Map.FakeDungeon = Dungeon;
        return &Map;
    }
    bool IsInCombat() const { return Combat; }
    bool IsBeingTeleported() const { return Teleporting; }
    bool IsAlive() const { return Alive; }
    WorldSession Session;
    PlayerbotAI AI;
    ObjectGuid GetGUID() const { return {Guid}; }
    WorldSession* GetSession() { return HasSession ? &Session : nullptr; }
    uint32 GetGuildId() const { return Guild; }
    bool GetGroup() const { return Grouped; }
    bool GetOriginalGroup() const { return OriginalGrouped; }
};
inline Player FakePlayer;
inline bool FakeClientAccountSession = false;
struct WorldSessionMgr
{
    WorldSession* FindSession(uint32 account)
    {
        return FakeClientAccountSession && account == FakePlayer.Session.Account ? &FakePlayer.Session : nullptr;
    }
};
inline WorldSessionMgr FakeSessionMgr;
#define sWorldSessionMgr (&FakeSessionMgr)
namespace ObjectAccessor
{
inline Player* FindConnectedPlayer(ObjectGuid guid)
{
    return FakePlayer.Online && guid.Value == FakePlayer.Guid ? &FakePlayer : nullptr;
}
}  // namespace ObjectAccessor
struct CharacterCache
{
    struct Entry
    {
        uint32 AccountId = 7, GuildId = 0;
        uint8 Class = 1;
        ObjectGuid GroupGuid;
        std::string Name = "Fixture";
        uint32 Faction = 0;
    };
    std::unordered_map<uint32, Entry> Entries;
    mutable Entry Fallback;
    std::optional<uint32> Account;
    std::optional<uint32> Guild;
    std::optional<uint32> Group;
    Entry const* GetCharacterCacheByGuid(ObjectGuid guid) const
    {
        auto found = Entries.find(guid.Value);
        if (found != Entries.end())
            return &found->second;
        Fallback.AccountId = Account.value_or(FakePlayer.Session.Account);
        Fallback.GuildId = Guild.value_or(FakePlayer.Guild);
        Fallback.GroupGuid = {Group.value_or(FakePlayer.Grouped ? 99u : 0u)};
        return &Fallback;
    }
    uint32 GetCharacterAccountIdByGuid(ObjectGuid guid) const { return GetCharacterCacheByGuid(guid)->AccountId; }
    uint32 GetCharacterGuildIdByGuid(ObjectGuid guid) const { return GetCharacterCacheByGuid(guid)->GuildId; }
    ObjectGuid GetCharacterGroupGuidByGuid(ObjectGuid guid) const { return GetCharacterCacheByGuid(guid)->GroupGuid; }
    uint32 GetCharacterTeamByGuid(ObjectGuid guid) const { return GetCharacterCacheByGuid(guid)->Faction; }
};
inline CharacterCache FakeCharacterCache;
#define sCharacterCache (&FakeCharacterCache)
#define GET_PLAYERBOT_AI(player) ((player)->HasAI ? &(player)->AI : nullptr)
inline bool IsSelfBot(Player* player) { return player->Selfbot; }
inline bool IsRealPlayer(Player* player) { return player->Human; }
struct PlayerbotAIConfig
{
    std::unordered_set<uint32> Accounts{7};
    bool IsInRandomAccountList(uint32 account) const { return Accounts.count(account); }
};
inline PlayerbotAIConfig sPlayerbotAIConfig;
inline bool FakePendingLogin = false;
inline unsigned FakeLogoutCalls = 0;
inline unsigned FakeLoginCalls = 0;
struct PlayerbotHolder
{
    static bool HasPendingBotLogin(ObjectGuid) { return FakePendingLogin; }
};
struct RandomPlayerbotMgr : PlayerbotHolder
{
    bool SnapshotReady = true;
    std::unordered_set<uint32> ExtraDedicatedAccounts;
    std::unordered_set<uint32> ExcludedAccounts;
    bool DedicatedAccountSnapshotReady() const { return SnapshotReady; }
    bool IsDedicatedRandomAccount(uint32 id) const
    {
        return SnapshotReady && (sPlayerbotAIConfig.Accounts.count(id) || ExtraDedicatedAccounts.count(id)) &&
               !ExcludedAccounts.count(id);
    }
    Player* GetPlayerBot(ObjectGuid guid) { return ObjectAccessor::FindConnectedPlayer(guid); }
    void LogoutPlayerBot(ObjectGuid)
    {
        ++FakeLogoutCalls;
        FakePlayer.Online = false;
    }
    void AddPlayerBot(ObjectGuid, uint32) { ++FakeLoginCalls; }
};
inline RandomPlayerbotMgr sRandomPlayerbotMgr;
inline std::string FakeLastError;
template <typename... Args>
std::string FakeFormat(std::string format, Args const&... args)
{
    [[maybe_unused]] auto append = [&](auto const& value)
    {
        auto const at = format.find("{}");
        if (at != std::string::npos)
        {
            std::ostringstream text;
            text << value;
            format.replace(at, 2, text.str());
        }
    };
    (append(args), ...);
    return format;
}
#define LOG_ERROR(category, ...) (FakeLastError = FakeFormat(__VA_ARGS__))
#define LOG_INFO(...) ((void)0)
#endif
