#include "RosterPlanner.h"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace PlayerbotAutonomy
{
char const* RoleName(CombatRole role)
{
    switch (role)
    {
        case CombatRole::Tank:
            return "tank";
        case CombatRole::Healer:
            return "healer";
        case CombatRole::Dps:
            return "dps";
        default:
            return "unknown";
    }
}
CombatRole EvidenceRole(bool tank, bool healer, bool dps, bool unambiguousSpec)
{
    if (!unambiguousSpec || unsigned(tank) + unsigned(healer) + unsigned(dps) != 1)
        return CombatRole::Unknown;
    return tank ? CombatRole::Tank : healer ? CombatRole::Healer : CombatRole::Dps;
}
bool RosterRules::Valid() const
{
    return MinLevel >= 1 && MinLevel <= MaxLevel && MaxLevel <= 80 && LevelSpread <= 20 && MinItemLevel >= 1 &&
           MinItemLevel <= 500 && MaxAge >= 5 && MaxAge <= 300 && MaxCandidates >= 5 && MaxCandidates <= 512;
}
std::string RosterRejection(RosterMember const& m, RosterRules const& r, std::uint32_t guild, std::uint32_t faction,
                            std::uint64_t now, std::uint64_t ownOperation)
{
    if (!m.Guid || !m.Account || !m.Guild || !m.Identity)
        return "unknown_identity";
    if (!m.Managed)
        return "unmanaged_or_human_guild";
    if (!m.Dedicated || m.HumanControlled)
        return "human_selfbot_or_unowned_session";
    if (!m.Persistent)
        return "m1_not_persistent";
    if (!m.Class || m.Class > 11 || m.Class == 10)
        return "unknown_or_ineligible_class";
    if (!m.ObservedAt || m.ObservedAt > now || now - m.ObservedAt > r.MaxAge)
        return "stale_roster";
    if (m.Faction != faction || faction > 1)
        return "incompatible_faction";
    if (!r.CrossGuild && m.Guild != guild)
        return "cross_guild_disabled";
    if (m.Reservation && m.Reservation != ownOperation)
        return "reservation_conflict";
    if (m.Grouped)
        return "group_conflict";
    if (m.Queued)
        return "queue_conflict";
    if (m.InInstance)
        return "instance_conflict";
    if (!m.Online)
        return "offline";
    if (!m.Available)
        return m.Restriction.empty() ? "unavailable" : m.Restriction;
    if (m.Level < r.MinLevel || m.Level > r.MaxLevel)
        return "level_mismatch";
    if (!m.EquipmentKnown || !m.EquippedItemLevel)
        return "inadequate_equipment_evidence";
    if (m.EquippedItemLevel < r.MinItemLevel)
        return "equipment_below_threshold";
    if (m.Role != CombatRole::Tank && m.Role != CombatRole::Healer && m.Role != CombatRole::Dps)
        return "ambiguous_or_unknown_role";
    return {};
}
PartyPlan PlanParty(std::vector<RosterMember> const& roster, RosterRules const& r, std::uint32_t guild,
                    std::uint32_t faction, std::uint64_t now)
{
    PartyPlan plan;
    plan.Guild = guild;
    plan.Faction = faction;
    if (!r.Valid() || !guild || faction > 1)
    {
        plan.Reason = "invalid_configuration_or_guild";
        return plan;
    }
    // Refuse truncated evidence rather than claiming a missing role in an
    // unexamined tail. Runtime caches are capped independently.
    if (roster.size() > r.MaxCandidates)
    {
        plan.Reason = "candidate_budget_exceeded";
        return plan;
    }
    std::vector<RosterMember> eligible;
    std::set<std::uint32_t> seen;
    for (auto const& m : roster)
    {
        ++plan.Considered;
        auto reason = RosterRejection(m, r, guild, faction, now);
        if (!seen.insert(m.Guid).second)
            reason = "duplicate_character";
        if (!reason.empty())
        {
            ++plan.Rejections[reason];
            continue;
        }
        eligible.push_back(m);
        if (m.Role == CombatRole::Tank)
            ++plan.Tanks;
        if (m.Role == CombatRole::Healer)
            ++plan.Healers;
        if (m.Role == CombatRole::Dps)
            ++plan.Dps;
    }
    std::sort(eligible.begin(), eligible.end(), [guild](auto const& a, auto const& b)
              { return std::make_tuple(a.Guild != guild, a.Guid) < std::make_tuple(b.Guild != guild, b.Guid); });
    unsigned bestForeign = 6;
    // At most 80 * 512 simple comparisons, independent of realm population.
    for (std::uint32_t low = r.MinLevel; low <= r.MaxLevel; ++low)
    {
        std::array<unsigned, 4> counts{};
        std::vector<RosterMember> selected;
        unsigned foreign = 0;
        for (auto const& m : eligible)
        {
            if (m.Level < low || m.Level > low + r.LevelSpread)
                continue;
            auto index = static_cast<unsigned>(m.Role);
            unsigned need = m.Role == CombatRole::Dps ? 3 : 1;
            if (counts[index] >= need)
                continue;
            ++counts[index];
            selected.push_back(m);
            foreign += m.Guild != guild;
        }
        if (selected.size() == 5 && foreign < bestForeign)
        {
            bestForeign = foreign;
            plan.Members = std::move(selected);
        }
    }
    if (!plan.Members.empty())
    {
        auto tank = std::find_if(plan.Members.begin(), plan.Members.end(),
                                 [](auto const& m) { return m.Role == CombatRole::Tank; });
        plan.Leader = tank->Guid;
        plan.Reason = "eligible";
    }
    else if (!plan.Tanks)
        plan.Reason = "missing_tank";
    else if (!plan.Healers)
        plan.Reason = "missing_healer";
    else if (plan.Dps < 3)
        plan.Reason = "missing_dps";
    else
        plan.Reason = "level_mismatch";
    return plan;
}
}  // namespace PlayerbotAutonomy
