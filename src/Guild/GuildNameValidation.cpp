#include <iomanip>
#include <sstream>

#include "GuildNamePool.h"
#include "ObjectMgr.h"
#include "Util.h"
#include "World.h"
namespace PlayerbotAutonomy
{
std::string CoreGuildNameRejection(std::string const& name)
{
    if (ObjectMgr::IsValidCharterName(name))
        return {};
    std::wstring wide;
    if (!Utf8toWStr(name, wide))
        return "core_invalid_utf8";
    if (wide.size() > MAX_CHARTER_NAME)
        return "core_max_characters=" + std::to_string(MAX_CHARTER_NAME) + " actual=" + std::to_string(wide.size());
    auto minimum = sWorld->getIntConfig(CONFIG_MIN_CHARTER_NAME);
    if (wide.size() < minimum)
        return "core_min_characters=" + std::to_string(minimum) + " actual=" + std::to_string(wide.size());
    if (sObjectMgr->IsReservedName(name))
        return "core_reserved_name";
    if (sObjectMgr->IsProfanityName(name))
        return "core_profanity_name";
    for (auto c : wide)
        if (!isExtendedLatinCharacter(c) && !isCyrillicCharacter(c) && !isEastAsianCharacter(c) && !isNumericOrSpace(c))
        {
            std::ostringstream reason;
            reason << "core_disallowed_character=U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                   << static_cast<unsigned>(c);
            return reason.str();
        }
    if (!isExtendedLatinString(wide, true) && !isCyrillicString(wide, true) && !isEastAsianString(wide, true))
        return "core_mixed_script";
    return "core_realm_language_policy strict_mask=" +
           std::to_string(sWorld->getIntConfig(CONFIG_STRICT_CHARTER_NAMES));
}
}  // namespace PlayerbotAutonomy
