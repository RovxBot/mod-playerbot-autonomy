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
        if constexpr (std::is_same_v<T, std::string>)
            return std::get<std::string>(Data);
        else
            return static_cast<T>(std::get<uint64>(Data));
    }
};
class PreparedResultSet
{
public:
    std::vector<std::vector<Field>> Rows;
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
    WorldSession Session;
    PlayerbotAI AI;
    ObjectGuid GetGUID() const { return {Guid}; }
    WorldSession* GetSession() { return HasSession ? &Session : nullptr; }
    uint32 GetGuildId() const { return Guild; }
    bool GetGroup() const { return Grouped; }
    bool GetOriginalGroup() const { return OriginalGrouped; }
};
inline Player FakePlayer;
namespace ObjectAccessor
{
inline Player* FindConnectedPlayer(ObjectGuid guid)
{
    return FakePlayer.Online && guid.Value == FakePlayer.Guid ? &FakePlayer : nullptr;
}
}  // namespace ObjectAccessor
struct CharacterCache
{
    std::optional<uint32> Account;
    std::optional<uint32> Guild;
    std::optional<uint32> Group;
    uint32 GetCharacterAccountIdByGuid(ObjectGuid) const { return Account.value_or(FakePlayer.Session.Account); }
    uint32 GetCharacterGuildIdByGuid(ObjectGuid) const { return Guild.value_or(FakePlayer.Guild); }
    ObjectGuid GetCharacterGroupGuidByGuid(ObjectGuid) const { return {Group.value_or(FakePlayer.Grouped ? 99u : 0u)}; }
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
