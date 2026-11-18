#include <charconv>
#include <sstream>

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "GuildScript.h"
#include "Log.h"
#include "Persistence/EnrollmentRuntime.h"
#include "PlayerScript.h"
#include "RBAC.h"
#include "WorldScript.h"

namespace
{
class PlayerbotAutonomyWorldScript final : public WorldScript
{
public:
    PlayerbotAutonomyWorldScript()
        : WorldScript("PlayerbotAutonomyWorldScript",
                      {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                       WORLDHOOK_ON_AFTER_UNLOAD_ALL_MAPS})
    {
        // Installed during script registration, before any WorldScript initializes
        // bots. The enabled but unloaded provider vetoes unknown candidates.
        Configure();
    }

    void OnStartup() override
    {
        PlayerbotAutonomy::Runtime().Start(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""));
    }

    void OnUpdate(uint32 diff) override { PlayerbotAutonomy::Runtime().Update(diff); }
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            Configure();
    }
    void OnAfterUnloadAllMaps() override { PlayerbotAutonomy::Runtime().Stop(); }

private:
    static void Configure()
    {
        PlayerbotAutonomy::Runtime().Configure(sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Enable", false));
    }
};

class EnrollmentInvalidationScript final : public GuildScript
{
public:
    EnrollmentInvalidationScript() : GuildScript("AutonomyEnrollmentGuildInvalidation", {GUILDHOOK_ON_EVENT}) {}
    void OnEvent(Guild*, uint8, ObjectGuid::LowType first, ObjectGuid::LowType second, uint8) override
    {
        // OnEvent has GUIDs for offline members too; OnAddMember may have a null
        // Player*.
        if (!PlayerbotAutonomy::Runtime().Cache().Enabled())
            return;
        if (first)
            PlayerbotAutonomy::Runtime().Invalidate(first);
        if (second)
            PlayerbotAutonomy::Runtime().Invalidate(second);
    }
};

class EnrollmentPlayerInvalidationScript final : public PlayerScript
{
public:
    EnrollmentPlayerInvalidationScript()
        : PlayerScript("AutonomyEnrollmentPlayerInvalidation", {PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_DELETE})
    {
    }
    void OnPlayerLogin(Player* player) override
    {
        if (PlayerbotAutonomy::Runtime().Cache().Enabled())
            PlayerbotAutonomy::Runtime().Invalidate(player->GetGUID().GetCounter());
    }
    void OnPlayerDelete(ObjectGuid guid, uint32) override
    {
        if (PlayerbotAutonomy::Runtime().Cache().Enabled())
            PlayerbotAutonomy::Runtime().Invalidate(guid.GetCounter());
    }
};

class EnrollmentCommands final : public CommandScript
{
public:
    EnrollmentCommands() : CommandScript("AutonomyEnrollmentCommands") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        // CLI/RA/SOAP commands bypass normal in-game RBAC checks. Require an
        // administrator-equivalent command permission for metadata, plus a
        // separate explicit configuration gate for lifecycle mutations.
        return {{"autonomy", Handle, rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE, Console::Yes}};
    }

private:
    static bool Handle(ChatHandler* handler, char const* args)
    {
        if (handler->GetSession())
            return false;  // Console/RA only. No player or GM chat enrollment
                           // authority.
        using namespace PlayerbotAutonomy;
        std::istringstream input(args ? args : "");
        std::string action;
        input >> action;
        auto& runtime = Runtime();
        if (action == "diagnose")
        {
            // The core erases CLI/RA/SOAP caller identity before dispatch. Fail
            // closed if RA admits accounts below administrator security, since
            // command RBAC metadata cannot enforce that boundary for CliHandler.
            if (sConfigMgr->GetOption<int32>("Ra.MinLevel", 3) < SEC_ADMINISTRATOR)
            {
                handler->SendSysMessage("Diagnosis unavailable: Ra.MinLevel must require administrator access (3+).");
                return true;
            }
            std::string token, extra;
            uint32 guid = 0;
            if (!(input >> token) || (input >> extra))
                return false;
            auto const parsed = std::from_chars(token.data(), token.data() + token.size(), guid);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !guid)
                return false;
            handler->SendSysMessage(FormatStageDiagnosis(runtime.DiagnoseStage(guid)));
            return true;
        }
        if (action == "status")
        {
            auto const& counts = runtime.Counters();
            handler->PSendSysMessage(
                "Autonomy activities={} protection={} maintenance_v1={} ready={} pages={} rows={} "
                "pending_observations={} orphan_observations={} commits={} "
                "replays={} rejected={} failures={} expired_reservations={} point_refreshes={} dirty_guids={}",
                runtime.ActivityEnabled(), runtime.Cache().Enabled(), runtime.ProtectionAvailable(),
                runtime.Cache().Ready(), counts.Pages, counts.Rows, counts.Pending, counts.Orphans, counts.Committed,
                counts.Replayed, counts.Rejected, counts.Failures, counts.ExpiredReservations, counts.Refreshes,
                runtime.Cache().DirtyCount());
            return true;
        }
        TransitionRequest request;
        if (action == "stage")
            request.Action = EnrollmentRequest::StageBeforeGuildJoin;
        else if (action == "confirm")
            request.Action = EnrollmentRequest::ConfirmAfterGuildJoin;
        else if (action == "cancel")
            request.Action = EnrollmentRequest::CancelFailedGuildJoin;
        else if (action == "retire")
            request.Action = EnrollmentRequest::ExplicitRetirement;
        else
            return false;
        // The target core's CLI dispatcher treats local console, authenticated
        // RA and admin SOAP alike, regardless of the RBAC command ID. Mutating
        // enrollment is therefore a distinct operator opt-in, never implied
        // by enabling the maintenance-protection provider.
        if (!sConfigMgr->GetOption<bool>("PlayerbotAutonomy.AllowEnrollmentCommands", false))
        {
            handler->SendSysMessage(
                "Enrollment mutations disabled. Set PlayerbotAutonomy.AllowEnrollmentCommands=1 "
                "only for an authorized, supervised transition.");
            return true;
        }
        if (!(input >> request.Guid >> request.ExpectedRevision >> request.Guild >> request.RequestId))
            return false;
        std::getline(input >> std::ws, request.Reason);
        if (request.Reason.empty() || request.Reason.size() > 255 || !ValidRequestId(request.RequestId))
            return false;
        if (!runtime.Submit(std::move(request)))
        {
            handler->SendSysMessage(
                "Enrollment rejected: disabled, missing maintenance capability, "
                "cache not ready, busy, or invalid identity.");
            return true;
        }
        handler->SendSysMessage(
            "Enrollment request queued. Verify its durable receipt in the module "
            "log and wait for ready status before any guild change; this command "
            "never changes guilds or progression.");
        return true;
    }
};
}  // namespace

void AddSC_playerbot_autonomy_module()
{
    new PlayerbotAutonomyWorldScript();
    new EnrollmentInvalidationScript();
    new EnrollmentPlayerInvalidationScript();
    new EnrollmentCommands();
}
