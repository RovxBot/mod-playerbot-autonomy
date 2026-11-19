#include <charconv>
#include <sstream>

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Operations/OperationRuntime.h"
#include "RBAC.h"
#include "WorldScript.h"
namespace
{
class AutonomyOperationWorld final : public WorldScript
{
public:
    AutonomyOperationWorld()
        : WorldScript("AutonomyOperationWorld", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE,
                                                 WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_AFTER_UNLOAD_ALL_MAPS})
    {
        Configure();
    }
    void OnStartup() override
    {
        PlayerbotAutonomy::Operations().Start(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""));
    }
    void OnUpdate(uint32 diff) override { PlayerbotAutonomy::Operations().Update(diff); }
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            Configure();
    }
    void OnAfterUnloadAllMaps() override { PlayerbotAutonomy::Operations().Stop(); }

private:
    static void Configure()
    {
        PlayerbotAutonomy::OperationSettings s;
        s.Enabled = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Enable", false) &&
                    sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Operations.Enable", false);
        s.AssembleGroups = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Operations.AssembleGroups", false);
        s.DryRun = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Operations.DryRun", true);
        s.MaxConcurrent = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MaxConcurrent", 1);
        s.IntervalMs = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.IntervalMs", 5000);
        s.RefreshBudget = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.RefreshPerSecond", 4);
        s.LeaseSeconds = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.LeaseSeconds", 120);
        s.TimeoutSeconds = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.TimeoutSeconds", 90);
        s.Rules.CrossGuild = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Operations.CrossGuild", false);
        s.Rules.MinLevel = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MinLevel", 15);
        s.Rules.MaxLevel = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MaxLevel", 80);
        s.Rules.LevelSpread = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.LevelSpread", 5);
        s.Rules.MinItemLevel = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MinItemLevel", 1);
        s.Rules.MaxAge = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MaxSnapshotAgeSeconds", 30);
        s.Rules.MaxCandidates = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Operations.MaxCandidates", 512);
        PlayerbotAutonomy::Operations().Configure(s);
    }
};
class AutonomyOperationCommands final : public CommandScript
{
public:
    AutonomyOperationCommands() : CommandScript("AutonomyOperationCommands") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        return {{"autonomy-operation", Handle, rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE, Console::Yes}};
    }

private:
    static bool Handle(ChatHandler* handler, char const* args)
    {
        if (handler->GetSession())
            return false;
        if (sConfigMgr->GetOption<int32>("Ra.MinLevel", 3) < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage(
                "Operation diagnostics require administrator-only RA admission (Ra.MinLevel >= 3).");
            return true;
        }
        std::istringstream in(args ? args : "");
        std::string action, token, extra;
        in >> action;
        if (action != "status" && action != "plan")
            return false;
        uint32 guild = 0;
        if (in >> token)
        {
            auto p = std::from_chars(token.data(), token.data() + token.size(), guild);
            if (p.ec != std::errc{} || p.ptr != token.data() + token.size() || !guild)
                return false;
        }
        if ((action == "plan" && !guild) || (in >> extra))
            return false;
        handler->SendSysMessage(PlayerbotAutonomy::Operations().Diagnose(guild));
        return true;
    }
};
}  // namespace
void AddSC_playerbot_autonomy_operation_module()
{
    new AutonomyOperationWorld();
    new AutonomyOperationCommands();
}
