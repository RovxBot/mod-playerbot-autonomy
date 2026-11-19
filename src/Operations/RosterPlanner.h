#ifndef PLAYERBOT_AUTONOMY_ROSTER_PLANNER_H
#define PLAYERBOT_AUTONOMY_ROSTER_PLANNER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace PlayerbotAutonomy
{
enum class CombatRole : std::uint8_t
{
    Unknown,
    Tank,
    Healer,
    Dps
};
char const* RoleName(CombatRole role);
CombatRole EvidenceRole(bool tank, bool healer, bool dps, bool unambiguousSpec);

struct RosterMember
{
    std::uint32_t Guid = 0, Account = 0, Guild = 0, Faction = 2, Class = 0, Level = 0;
    std::uint32_t Spec = 0, EquippedItemLevel = 0;
    std::uint64_t ObservedAt = 0, Reservation = 0;
    CombatRole Role = CombatRole::Unknown;
    bool Managed = false, Dedicated = false, Persistent = false, Identity = false;
    bool HumanControlled = true, Online = false, Available = false, EquipmentKnown = false;
    bool Grouped = false, Queued = false, InInstance = false;
    std::map<std::uint32_t, std::uint32_t> Professions;
    std::string Restriction;
};
struct RosterRules
{
    bool CrossGuild = false;
    std::uint32_t MinLevel = 15, MaxLevel = 80, LevelSpread = 5, MinItemLevel = 1;
    std::uint32_t MaxCandidates = 512;
    std::uint64_t MaxAge = 30;
    bool Valid() const;
};
struct PartyPlan
{
    std::uint32_t Guild = 0, Faction = 2, Leader = 0;
    std::vector<RosterMember> Members;
    std::map<std::string, std::uint32_t> Rejections;
    std::string Reason;
    std::uint32_t Tanks = 0, Healers = 0, Dps = 0, Considered = 0;
    bool Complete() const { return Members.size() == 5 && Reason == "eligible"; }
};
std::string RosterRejection(RosterMember const& member, RosterRules const& rules, std::uint32_t guild,
                            std::uint32_t faction, std::uint64_t now, std::uint64_t ownOperation = 0);
PartyPlan PlanParty(std::vector<RosterMember> const& roster, RosterRules const& rules, std::uint32_t guild,
                    std::uint32_t faction, std::uint64_t now);
}  // namespace PlayerbotAutonomy
#endif
