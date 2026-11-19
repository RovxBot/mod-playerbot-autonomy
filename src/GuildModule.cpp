#include <charconv>
#include <sstream>

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Guild/GuildRuntime.h"
#include "RBAC.h"
#include "WorldScript.h"
namespace
{
class AutonomyGuildWorld final : public WorldScript
{
public:
    AutonomyGuildWorld()
        : WorldScript("AutonomyGuildWorld", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                                             WORLDHOOK_ON_AFTER_UNLOAD_ALL_MAPS})
    {
        Configure();
    }
    void OnStartup() override
    {
        PlayerbotAutonomy::Guilds().Start(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""));
    }
    void OnUpdate(uint32 diff) override { PlayerbotAutonomy::Guilds().Update(diff); }
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            Configure();
    }
    void OnAfterUnloadAllMaps() override { PlayerbotAutonomy::Guilds().Stop(); }

private:
    static void Configure()
    {
        PlayerbotAutonomy::GuildSettings s;
        s.Enabled = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Enable", false) &&
                    sConfigMgr->GetOption<bool>("PlayerbotAutonomy.GuildDirector.Enable", false);
        s.DryRun = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.GuildDirector.DryRun", true);
        s.Target = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.GuildCount", 8);
        s.AlliancePercent = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.AlliancePercent", 50);
        s.MaxMembers = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.MaxMembers", 70);
        s.CandidateBudget = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.CandidatesPerTick", 16);
        s.IntervalMs = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.IntervalMs", 1000);
        s.CooldownMs = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.FoundingCooldownMs", 60000);
        s.ClassMask = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.GuildDirector.ClassMask", 0x5ff);
        PlayerbotAutonomy::Guilds().Configure(s);
        PlayerbotAutonomy::Guilds().ConfigureRecovery(
            sConfigMgr->GetOption<bool>("PlayerbotAutonomy.AllowGuildRecoveryCommands", false));
        PlayerbotAutonomy::RecruitmentSettings r;
        r.Enabled = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Recruitment.Enable", false);
        r.DryRun = sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Recruitment.DryRun", true);
        r.CooldownMs = sConfigMgr->GetOption<uint32>("PlayerbotAutonomy.Recruitment.CooldownMs", 15000);
        r.IntervalMs = s.IntervalMs;
        r.CandidateBudget = s.CandidateBudget;
        PlayerbotAutonomy::Guilds().ConfigureRecruitment(r);
    }
};
class AutonomyGuildCommands final : public CommandScript
{
public:
    AutonomyGuildCommands() : CommandScript("AutonomyGuildCommands") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        return {{"autonomy-guild", Handle, rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE, Console::Yes}};
    }

private:
    static bool Handle(ChatHandler* handler, char const* args)
    {
        if (handler->GetSession())
            return false;
        if (sConfigMgr->GetOption<int32>("Ra.MinLevel", 3) < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage("Guild diagnostics require administrator-only RA admission (Ra.MinLevel >= 3).");
            return true;
        }
        std::istringstream in(args ? args : "");
        std::string action, token, extra;
        uint32 guid = 0;
        in >> action;
        auto number = [&](uint32& value)
        {
            if (!(in >> token))
                return false;
            auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
        };
        if (action == "audit")
        {
            if (in >> token)
            {
                auto parsed = std::from_chars(token.data(), token.data() + token.size(), guid);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                    return false;
            }
            if (in >> extra)
                return false;
            handler->SendSysMessage(PlayerbotAutonomy::Guilds().Audit(guid));
            return true;
        }
        if (action == "names")
        {
            if (in >> extra)
                return false;
            handler->SendSysMessage(PlayerbotAutonomy::Guilds().Names());
            return true;
        }
        if (action == "plan")
        {
            uint32 slot = 0;
            if (!number(guid) || !guid || !number(slot) || (in >> extra))
                return false;
            handler->SendSysMessage(PlayerbotAutonomy::Guilds().Plan(guid, slot));
            return true;
        }
        if (action == "recovery-diagnose" || action == "recover")
        {
            uint32 slot = 0, guild = 0;
            if (!number(slot) || !slot || !number(guild) || !guild)
                return false;
            if (action == "recovery-diagnose")
            {
                if (in >> extra)
                    return false;
                handler->SendSysMessage(PlayerbotAutonomy::Guilds().RecoveryDiagnosis(slot, guild));
                return true;
            }
            std::string founderName, approval;
            if (!(in >> founderName >> approval) || (in >> extra))
                return false;
            if (!sConfigMgr->GetOption<bool>("PlayerbotAutonomy.AllowGuildRecoveryCommands", false))
            {
                handler->SendSysMessage(
                    "Recovery writes disabled; operator must explicitly enable AllowGuildRecoveryCommands.");
                return true;
            }
            handler->SendSysMessage(PlayerbotAutonomy::Guilds().Recover(slot, guild, founderName, approval));
            return true;
        }
        if (action == "recruit-plan")
        {
            uint32 guild = 0, actor = 0;
            if (!number(guid) || !guid || !number(guild) || !guild || !number(actor) || !actor || (in >> extra))
                return false;
            handler->SendSysMessage(PlayerbotAutonomy::Guilds().RecruitmentPlan(guid, guild, actor));
            return true;
        }
        return false;
    }
};
}  // namespace
void AddSC_playerbot_autonomy_guild_module()
{
    new AutonomyGuildWorld();
    new AutonomyGuildCommands();
}
