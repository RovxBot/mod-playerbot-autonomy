#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_TRANSITION_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_TRANSITION_H

#include <cstdint>
#include <limits>
#include <string>

#include "EnrollmentState.h"

namespace PlayerbotAutonomy
{
char const* StateName(EnrollmentState state);
EnrollmentState ParseState(std::string const& state);
bool ValidRequestId(std::string const& id);

struct TransitionRequest
{
    std::uint32_t Guid = 0;
    std::uint32_t Account = 0;
    std::uint32_t Guild = 0;
    std::uint64_t ExpectedRevision = 0;
    EnrollmentRequest Action = EnrollmentRequest::StageBeforeGuildJoin;
    std::string RequestId;
    std::string Reason;
};

struct TransitionReceipt
{
    std::uint32_t Guid = 0;
    std::uint32_t Account = 0;
    std::uint32_t Guild = 0;
    std::uint64_t ExpectedRevision = 0;
    std::uint64_t ResultingRevision = 0;
    std::string Event;
    EnrollmentState Outcome = EnrollmentState::Unknown;
    std::string ActorKind;
    std::uint32_t ActorGuid = 0;
    std::string Reason;
};

inline char const* EventName(EnrollmentRequest request)
{
    switch (request)
    {
        case EnrollmentRequest::StageBeforeGuildJoin:
            return "STAGE";
        case EnrollmentRequest::ConfirmAfterGuildJoin:
            return "CONFIRM";
        case EnrollmentRequest::CancelFailedGuildJoin:
            return "CANCEL";
        case EnrollmentRequest::ExplicitRetirement:
            return "RETIRE";
    }
    return "INVALID";
}

inline bool MatchesReceipt(TransitionRequest const& request, TransitionReceipt const& receipt)
{
    if (request.ExpectedRevision == std::numeric_limits<std::uint64_t>::max())
        return false;
    return receipt.Guid == request.Guid && receipt.Account == request.Account && receipt.Guild == request.Guild &&
           receipt.ExpectedRevision == request.ExpectedRevision &&
           receipt.ResultingRevision == request.ExpectedRevision + 1 && receipt.Event == EventName(request.Action) &&
           receipt.Outcome == StateAfterAcceptedTransition(request.Action) && receipt.ActorKind == "AUTHORIZED_ADMIN" &&
           receipt.ActorGuid == 0 && receipt.Reason == request.Reason;
}
}  // namespace PlayerbotAutonomy
#endif
