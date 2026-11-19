#ifndef PLAYERBOT_AUTONOMY_GUILD_NAME_POOL_H
#define PLAYERBOT_AUTONOMY_GUILD_NAME_POOL_H
#include <array>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace PlayerbotAutonomy
{
inline constexpr std::array<char const*, 46> GuildNamePool = {{"Numen",
                                                               "XS",
                                                               "ffs",
                                                               "uh oh",
                                                               "Welcome",
                                                               "CyLx-亚服",
                                                               "we go",
                                                               "ЦентуриТрайб",
                                                               "Rename",
                                                               "For Kalimdor",
                                                               "Therapy",
                                                               "built diff",
                                                               "Inept",
                                                               "The Unnamed",
                                                               "Ook Ook",
                                                               "No Pressure",
                                                               "NBO",
                                                               "HAZE GDKP",
                                                               "Pulse GDKP",
                                                               "暗流",
                                                               "一树梨花白",
                                                               "Aspire",
                                                               "good job",
                                                               "Epiphany",
                                                               "Nine Nine",
                                                               "Pulse",
                                                               "Disposed",
                                                               "Exploding Labrats",
                                                               "bililili",
                                                               "Seriously Bad",
                                                               "uh oh GDKP",
                                                               "stepbrothers GDKP",
                                                               "Submerge",
                                                               "Carnivores",
                                                               "Rename GDKP",
                                                               "Melee Mechanics",
                                                               "ED",
                                                               "Тиамат",
                                                               "*The Un*named GDKP",
                                                               "Лимит",
                                                               "fuse",
                                                               "WORST GDKP EUW",
                                                               "Hungry For More",
                                                               "ClassicPlus Waiting Room",
                                                               "Refuge",
                                                               "Danger Dolls"}};
struct GuildNameChoice
{
    std::string Name;
    std::vector<std::pair<std::string, std::string>> Rejected;
};
inline std::string TrimGuildName(std::string name)
{
    auto first = name.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos)
        return {};
    return name.substr(first, name.find_last_not_of(" \t\r\n\f\v") - first + 1);
}
template <class Pool, class Validate>
GuildNameChoice SelectGuildName(Pool const& pool, Validate validate)
{
    GuildNameChoice result;
    std::set<std::string> seen;
    for (auto const& entry : pool)
    {
        auto name = TrimGuildName(entry);
        std::string reason = validate(name);
        if (!seen.insert(name).second)
            reason = "duplicate_pool_candidate";
        if (!reason.empty())
            result.Rejected.emplace_back(name, reason);
        else if (result.Name.empty())
            result.Name = name;
    }
    return result;
}
// Runtime adapter delegates acceptance to the actual core. The explanation
// only identifies the failed core constraint; it cannot authorize a name.
std::string CoreGuildNameRejection(std::string const& name);
}  // namespace PlayerbotAutonomy
#endif
