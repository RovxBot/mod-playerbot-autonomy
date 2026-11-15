#ifndef PLAYERBOT_AUTONOMY_MAINTENANCE_POLICY_H
#define PLAYERBOT_AUTONOMY_MAINTENANCE_POLICY_H

#include "Policy/PopulationPolicy.h"

namespace PlayerbotAutonomy
{
// Engine-free specification of routine playerbots operations. This is not
// currently wired into mod-playerbots or the worldserver. Every new operation
// must be classified and tested before integrating an enforcement callback.
enum class MaintenanceAction
{
    FullRandomize,
    IncrementalRandomize,
    InitialRandomize,
    ForcedLevelChange,
    BracketReassignment,
    PlayedTimeLevelReset,
    LevelSkip,
    RoutineRefresh,
    RandomTeleport,
    ReviveRefreshAndTeleport,
    BotLedGroupTeardown,
    PopulationTrimLogout,
    AdministrativeClear,
    AutomaticGuildPlacement,

    // Ordinary character progression and economy must keep working.
    NaturalLevelGain,
    QuestAndLootProgress,
    GatherCraftAndAuction,
    GuildBankDeposit,
    NonDestructiveResurrection,
    Observation
};

struct MaintenanceContext
{
    bool AutonomyEnabled = false;
    PopulationSnapshot Character;
    bool HasExclusiveActivityLease = false;
};

// Whitelist only normal gameplay operations, and treat newly introduced enum
// values as disruptive until reviewed. The caller is responsible for checking
// the actual source operation, privilege and thread ownership.
constexpr bool IsOrdinaryGameplayAction(MaintenanceAction action)
{
    switch (action)
    {
        case MaintenanceAction::NaturalLevelGain:
        case MaintenanceAction::QuestAndLootProgress:
        case MaintenanceAction::GatherCraftAndAuction:
        case MaintenanceAction::GuildBankDeposit:
        case MaintenanceAction::NonDestructiveResurrection:
        case MaintenanceAction::Observation:
            return true;
        default:
            return false;
    }
}

// Return true if autonomy should veto an existing routine operation.
// false means DEFER TO EXISTING playerbots policy, not "authorize action".
// No DB access, map-object access, mutex, global state or dynamic allocation.
constexpr bool ShouldSuppressRoutineMaintenance(MaintenanceContext const& context, MaintenanceAction action)
{
    if (!context.AutonomyEnabled)
        return false;

    PopulationKind kind = ClassifyPopulation(context.Character);
    if (kind == PopulationKind::Unmanaged)
        return false;

    if (IsOrdinaryGameplayAction(action))
        return false;

    // The existing factory places guildless random bots in guilds at login.
    // Under autonomy, guild enrollment must be intentional and durable.
    // Future guild directors will use a separate authorized recruitment path.
    if (action == MaintenanceAction::AutomaticGuildPlacement)
        return true;

    // Guilded OR previously enrolled bots retain all earned progression. An
    // exclusive lease also prevents conflicting maintenance of world filler.
    return kind == PopulationKind::GuildPersistent || context.HasExclusiveActivityLease;
}
} // namespace PlayerbotAutonomy

#endif // PLAYERBOT_AUTONOMY_MAINTENANCE_POLICY_H
