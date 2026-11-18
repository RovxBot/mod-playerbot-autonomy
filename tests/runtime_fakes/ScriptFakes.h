#ifndef AUTONOMY_TEST_SCRIPT_FAKES_H
#define AUTONOMY_TEST_SCRIPT_FAKES_H
#include <initializer_list>
#include <string_view>

#include "CoreFakes.h"

constexpr int32 SEC_ADMINISTRATOR = 3;
namespace rbac
{
constexpr uint32 RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE = 1;
}
class ChatHandler
{
public:
    WorldSession* Session = nullptr;
    std::vector<std::string> Messages;
    WorldSession* GetSession() { return Session; }
    void SendSysMessage(std::string_view message) { Messages.emplace_back(message); }
    template <typename... Args>
    void PSendSysMessage(std::string format, Args const&... args)
    {
        Messages.push_back(FakeFormat(format, args...));
    }
};
namespace Acore::ChatCommands
{
enum class Console
{
    Yes,
    No
};
struct ChatCommand
{
    std::string Name;
    bool (*Handler)(ChatHandler*, char const*);
    uint32 Permission;
    Console AllowConsole;
};
using ChatCommandTable = std::vector<ChatCommand>;
}  // namespace Acore::ChatCommands
class CommandScript
{
public:
    explicit CommandScript(char const*) {}
    virtual ~CommandScript() = default;
    virtual Acore::ChatCommands::ChatCommandTable GetCommands() const = 0;
};
struct ConfigMgr
{
    std::unordered_map<std::string, int32> Values;
    template <typename T>
    T GetOption(std::string const& key, T fallback) const
    {
        if constexpr (std::is_integral_v<T>)
        {
            auto const found = Values.find(key);
            return found == Values.end() ? fallback : static_cast<T>(found->second);
        }
        else
            return fallback;
    }
};
inline ConfigMgr FakeConfig;
#define sConfigMgr (&FakeConfig)

enum Hooks
{
    WORLDHOOK_ON_STARTUP,
    WORLDHOOK_ON_UPDATE,
    WORLDHOOK_ON_AFTER_CONFIG_LOAD,
    WORLDHOOK_ON_AFTER_UNLOAD_ALL_MAPS,
    GUILDHOOK_ON_EVENT,
    PLAYERHOOK_ON_LOGIN,
    PLAYERHOOK_ON_DELETE
};
class WorldScript
{
public:
    WorldScript(char const*, std::initializer_list<Hooks>) {}
    virtual ~WorldScript() = default;
    virtual void OnStartup() {}
    virtual void OnUpdate(uint32) {}
    virtual void OnAfterConfigLoad(bool) {}
    virtual void OnAfterUnloadAllMaps() {}
};
struct Guild
{
};
class GuildScript
{
public:
    GuildScript(char const*, std::initializer_list<Hooks>) {}
    virtual ~GuildScript() = default;
    virtual void OnEvent(Guild*, uint8, ObjectGuid::LowType, ObjectGuid::LowType, uint8) {}
};
class PlayerScript
{
public:
    PlayerScript(char const*, std::initializer_list<Hooks>) {}
    virtual ~PlayerScript() = default;
    virtual void OnPlayerLogin(Player*) {}
    virtual void OnPlayerDelete(ObjectGuid, uint32) {}
};
#endif
