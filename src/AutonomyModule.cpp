#include "Config.h"
#include "Log.h"
#include "WorldScript.h"

namespace
{
class PlayerbotAutonomyWorldScript final : public WorldScript
{
public:
    PlayerbotAutonomyWorldScript()
        : WorldScript("PlayerbotAutonomyWorldScript", {WORLDHOOK_ON_STARTUP})
    {
    }

    void OnStartup() override
    {
        if (!sConfigMgr->GetOption<bool>("PlayerbotAutonomy.Enable", false))
        {
            LOG_INFO("module", "mod-playerbot-autonomy: disabled (foundation only)");
            return;
        }

        LOG_INFO("module", "mod-playerbot-autonomy: foundation enabled; no runtime features are implemented");
    }
};
} // namespace

void AddSC_playerbot_autonomy_module()
{
    new PlayerbotAutonomyWorldScript();
}
