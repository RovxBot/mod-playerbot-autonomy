#include "OperationCoordinator.h"

#include <set>

namespace PlayerbotAutonomy
{
char const* OperationStateName(OperationState state)
{
    switch (state)
    {
        case OperationState::Proposed:
            return "PROPOSED";
        case OperationState::Validated:
            return "VALIDATED";
        case OperationState::Reserved:
            return "RESERVED";
        case OperationState::Assembling:
            return "ASSEMBLING";
        case OperationState::Active:
            return "ACTIVE";
        case OperationState::Closing:
            return "CLOSING";
        case OperationState::Completed:
            return "COMPLETED";
        case OperationState::Failed:
            return "FAILED";
        case OperationState::Cancelled:
            return "CANCELLED";
        case OperationState::Recovering:
            return "RECOVERING";
    }
    return "FAILED";
}
bool Terminal(OperationState s)
{
    return s == OperationState::Completed || s == OperationState::Failed || s == OperationState::Cancelled;
}
bool CanTransition(OperationState from, OperationState to)
{
    if (Terminal(from))
        return false;
    if (to == OperationState::Recovering || to == OperationState::Cancelled || to == OperationState::Failed)
        return true;
    switch (from)
    {
        case OperationState::Proposed:
            return to == OperationState::Validated;
        case OperationState::Validated:
            return to == OperationState::Reserved;
        case OperationState::Reserved:
            return to == OperationState::Assembling;
        case OperationState::Assembling:
            return to == OperationState::Active;
        case OperationState::Active:
            return to == OperationState::Closing;
        case OperationState::Closing:
            return to == OperationState::Completed;
        default:
            return false;
    }
}
bool OperationSettings::Valid() const
{
    return !AssembleGroups && Rules.Valid() && MaxConcurrent >= 1 && MaxConcurrent <= 4 && IntervalMs >= 1000 &&
           IntervalMs <= 60000 && RefreshBudget >= 1 && RefreshBudget <= 16 && TimeoutSeconds >= 30 &&
           TimeoutSeconds <= 600 && LeaseSeconds > TimeoutSeconds && LeaseSeconds <= 900;
}
Operation NextOperation(Operation const& current, std::uint64_t now, bool enabled, bool valid)
{
    Operation next = current;
    if (Terminal(current.State))
        return next;
    if (current.State == OperationState::Recovering)
    {
        next.State = OperationState::Cancelled;
        next.Reason = "restart_reconciled_no_executor";
    }
    else if (!enabled)
    {
        next.State = OperationState::Cancelled;
        next.Reason = "feature_disabled";
    }
    else if (now >= current.Deadline || now >= current.LeaseUntil)
    {
        next.State = OperationState::Failed;
        next.Reason = "operation_timeout";
    }
    else if (!valid)
    {
        next.State = OperationState::Failed;
        next.Reason = "participant_changed_or_unavailable";
    }
    else
    {
        switch (current.State)
        {
            case OperationState::Proposed:
                next.State = OperationState::Validated;
                break;
            case OperationState::Validated:
                next.State = OperationState::Reserved;
                break;
            case OperationState::Reserved:
                next.State = OperationState::Assembling;
                break;
            case OperationState::Assembling:
                next.State = OperationState::Active;
                break;
            case OperationState::Active:
                next.State = OperationState::Closing;
                break;
            case OperationState::Closing:
                next.State = OperationState::Completed;
                next.Reason = "simulation_completed_no_group_created";
                break;
            default:
                next.State = OperationState::Failed;
                next.Reason = "unsupported_state";
                break;
        }
    }
    ++next.Revision;
    ++next.Attempts;
    next.ChangedAt = now;
    return next;
}
std::vector<std::uint32_t> ParticipantGuids(Operation const& operation)
{
    std::vector<std::uint32_t> result;
    for (auto const& m : operation.Members)
        result.push_back(m.Guid);
    return result;
}
bool SameOperation(Operation const& a, Operation const& b)
{
    if (a.Id != b.Id || a.Revision != b.Revision || a.State != b.State || a.Guild != b.Guild || a.Leader != b.Leader ||
        a.Faction != b.Faction || a.Type != b.Type || a.CreatedAt != b.CreatedAt || a.ChangedAt != b.ChangedAt ||
        a.Deadline != b.Deadline || a.LeaseUntil != b.LeaseUntil || a.Reason != b.Reason || a.Attempts != b.Attempts ||
        a.Members.size() != b.Members.size())
        return false;
    for (std::size_t i = 0; i < a.Members.size(); ++i)
    {
        auto const& x = a.Members[i];
        auto const& y = b.Members[i];
        if (x.Guid != y.Guid || x.Account != y.Account || x.Guild != y.Guild || x.Role != y.Role)
            return false;
    }
    return true;
}
}  // namespace PlayerbotAutonomy
