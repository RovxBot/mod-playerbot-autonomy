#include "Policy/PopulationPolicy.h"

#include <cassert>

using namespace PlayerbotAutonomy;

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
}
