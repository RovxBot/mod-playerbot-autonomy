#include "Policy/MaintenancePolicy.h"
#include "Policy/PopulationPolicy.h"

#include <array>
#include <cassert>

using namespace PlayerbotAutonomy;

namespace
{
constexpr std::array<MaintenanceAction, 14> kDisruptiveActions = {
    MaintenanceAction::FullRandomize,
    MaintenanceAction::IncrementalRandomize,
    MaintenanceAction::InitialRandomize,
    MaintenanceAction::ForcedLevelChange,
    MaintenanceAction::BracketReassignment,
    MaintenanceAction::PlayedTimeLevelReset,
    MaintenanceAction::LevelSkip,
    MaintenanceAction::RoutineRefresh,
    MaintenanceAction::RandomTeleport,
    MaintenanceAction::ReviveRefreshAndTeleport,
    MaintenanceAction::BotLedGroupTeardown,
    MaintenanceAction::PopulationTrimLogout,
    MaintenanceAction::AdministrativeClear,
    MaintenanceAction::AutomaticGuildPlacement,
};

constexpr std::array<MaintenanceAction, 6> kOrdinaryActions = {
    MaintenanceAction::NaturalLevelGain,
    MaintenanceAction::QuestAndLootProgress,
    MaintenanceAction::GatherCraftAndAuction,
    MaintenanceAction::GuildBankDeposit,
    MaintenanceAction::NonDestructiveResurrection,
    MaintenanceAction::Observation,
};

constexpr bool CheckAllActions(MaintenanceContext const& context, bool expectedDisruptive)
{
    for (MaintenanceAction action : kDisruptiveActions)
    {
        if (ShouldSuppressRoutineMaintenance(context, action) != expectedDisruptive)
            return false;
    }

    for (MaintenanceAction action : kOrdinaryActions)
    {
        if (ShouldSuppressRoutineMaintenance(context, action))
            return false;
    }
    return true;
}

constexpr bool CheckWorldFillerWithoutLease()
{
    MaintenanceContext context{true, {true, false, false, false, false}, false};
    for (MaintenanceAction action : kDisruptiveActions)
    {
        // Automatic guild placement must become an intentional, durable
        // transition. All other filler maintenance still follows legacy policy.
        bool expected = action == MaintenanceAction::AutomaticGuildPlacement;
        if (ShouldSuppressRoutineMaintenance(context, action) != expected)
            return false;
    }
    return true;
}
} // namespace

static_assert(CheckAllActions({false, {true, false, false, true, false}, true}, false),
              "master switch off preserves legacy behaviour even for guilded/leased bots");
static_assert(CheckAllActions({true, {true, false, false, true, false}, false}, true),
              "current guild members cannot be destructively maintained");
static_assert(CheckAllActions({true, {true, false, false, false, true}, false}, true),
              "durably enrolled bots remain protected after guild loss");
static_assert(CheckAllActions({true, {true, false, false, false, false}, true}, true),
              "leased filler cannot be randomly repurposed");
static_assert(CheckAllActions({true, {false, false, false, true, true}, true}, false),
              "real player characters remain unmanaged");
static_assert(CheckAllActions({true, {true, true, false, true, true}, true}, false),
              "selfbots remain unmanaged");
static_assert(CheckAllActions({true, {true, false, true, true, true}, true}, false),
              "human-controlled random bots remain unmanaged");
static_assert(CheckWorldFillerWithoutLease(), "world filler retains existing maintenance except auto-guilding");

int main()
{
    static_assert(ClassifyPopulation({}) == PopulationKind::Unmanaged);
    static_assert(ClassifyPopulation({true, false, false, false, false}) == PopulationKind::WorldFiller);
    static_assert(ClassifyPopulation({true, false, false, true, false}) == PopulationKind::GuildPersistent);
    static_assert(ClassifyPopulation({true, false, false, false, true}) == PopulationKind::GuildPersistent);
    static_assert(ClassifyPopulation({true, true, false, true, true}) == PopulationKind::Unmanaged);
    static_assert(ClassifyPopulation({true, false, true, true, true}) == PopulationKind::Unmanaged);
    static_assert(ClassifyPopulation({false, false, false, true, false}) == PopulationKind::Unmanaged);

    static_assert(CanDirectGuildOperations(PopulationKind::GuildPersistent, true, false));
    static_assert(!CanDirectGuildOperations(PopulationKind::GuildPersistent, false, false));
    static_assert(!CanDirectGuildOperations(PopulationKind::GuildPersistent, true, true));
    static_assert(!CanDirectGuildOperations(PopulationKind::WorldFiller, true, false));
    static_assert(!CanDirectGuildOperations(PopulationKind::Unmanaged, true, false));

    assert(ClassifyPopulation({true, false, false, false, false}) == PopulationKind::WorldFiller);
    assert(ClassifyPopulation({true, false, false, false, true}) == PopulationKind::GuildPersistent);
    assert(CheckWorldFillerWithoutLease());
    assert(CheckAllActions({true, {true, false, false, true, false}, false}, true));
    assert(CheckAllActions({false, {true, false, false, true, false}, true}, false));
}
