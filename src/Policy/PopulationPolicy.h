#ifndef PLAYERBOT_AUTONOMY_POPULATION_POLICY_H
#define PLAYERBOT_AUTONOMY_POPULATION_POLICY_H

namespace PlayerbotAutonomy
{
// This header is a pure, engine-independent policy prototype.
// It is NOT wired to random-bot resets or guild events yet.
enum class PopulationKind
{
    Unmanaged,          // Human characters, selfbots and human-controlled bots.
    WorldFiller,        // Unguilded random bots retain existing reroll/reset behaviour.
    GuildPersistent     // Guilded or previously enrolled random bots retain earned state.
};

struct PopulationSnapshot
{
    bool IsRandomBot = false;
    bool IsSelfBot = false;
    bool HasHumanController = false;
    bool HasGuild = false;
    bool WasPersistent = false;
};

constexpr PopulationKind ClassifyPopulation(PopulationSnapshot const& candidate)
{
    if (!candidate.IsRandomBot || candidate.IsSelfBot || candidate.HasHumanController)
        return PopulationKind::Unmanaged;

    if (candidate.HasGuild || candidate.WasPersistent)
        return PopulationKind::GuildPersistent;

    return PopulationKind::WorldFiller;
}

// Persistence and authority are separate policies. A random bot belonging to a
// human-managed guild retains its character but cannot be directed as part of
// an autonomous guild. The live implementation must derive these flags from
// authoritative guild membership and ownership, not a heuristic.
constexpr bool CanDirectGuildOperations(PopulationKind population, bool isBotOwnedGuild,
                                        bool containsHumanMembers)
{
    return population == PopulationKind::GuildPersistent && isBotOwnedGuild && !containsHumanMembers;
}
} // namespace PlayerbotAutonomy

#endif // PLAYERBOT_AUTONOMY_POPULATION_POLICY_H
