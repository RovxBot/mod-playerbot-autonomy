#include "EnrollmentTransition.h"

#include <cctype>

namespace PlayerbotAutonomy
{
char const* StateName(EnrollmentState state)
{
    switch (state)
    {
        case EnrollmentState::Absent:
            return "ABSENT";
        case EnrollmentState::Pending:
            return "PENDING";
        case EnrollmentState::Persistent:
            return "PERSISTENT";
        case EnrollmentState::Retired:
            return "RETIRED";
        default:
            return "UNKNOWN";
    }
}

EnrollmentState ParseState(std::string const& state)
{
    if (state == "ABSENT")
        return EnrollmentState::Absent;
    if (state == "PENDING")
        return EnrollmentState::Pending;
    if (state == "PERSISTENT")
        return EnrollmentState::Persistent;
    if (state == "RETIRED")
        return EnrollmentState::Retired;
    return EnrollmentState::Unknown;
}

bool ValidRequestId(std::string const& id)
{
    if (id.size() != 36)
        return false;
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (id[i] != '-')
                return false;
        }
        else if (!std::isdigit(static_cast<unsigned char>(id[i])) && (id[i] < 'a' || id[i] > 'f'))
            return false;
    }
    return true;
}

}  // namespace PlayerbotAutonomy
