#ifndef PLAYERBOT_AUTONOMY_OPERATION_COORDINATOR_H
#define PLAYERBOT_AUTONOMY_OPERATION_COORDINATOR_H

#include "RosterPlanner.h"

namespace PlayerbotAutonomy
{
enum class OperationState : std::uint8_t
{
    Proposed,
    Validated,
    Reserved,
    Assembling,
    Active,
    Closing,
    Completed,
    Failed,
    Cancelled,
    Recovering
};
enum class OperationType : std::uint8_t
{
    DungeonParty,
    OutdoorParty,
    Pvp,
    Raid
};
char const* OperationStateName(OperationState state);
bool Terminal(OperationState state);
bool CanTransition(OperationState from, OperationState to);
struct Operation
{
    std::uint64_t Id = 0, Revision = 0, CreatedAt = 0, ChangedAt = 0, Deadline = 0, LeaseUntil = 0;
    std::uint32_t Guild = 0, Leader = 0, Faction = 2, Attempts = 0;
    OperationType Type = OperationType::DungeonParty;
    OperationState State = OperationState::Proposed;
    std::vector<RosterMember> Members;
    std::string Reason = "simulated_dungeon_party";
};
struct OperationSettings
{
    bool Enabled = false;
    bool DryRun = true;           // Refresh/plan only; no new operation writes or leases.
    bool AssembleGroups = false;  // No executor is shipped; true fails closed.
    std::uint32_t MaxConcurrent = 1, IntervalMs = 5000, RefreshBudget = 4;
    std::uint32_t LeaseSeconds = 120, TimeoutSeconds = 90;
    RosterRules Rules;
    bool Valid() const;
};
// Pure lifecycle decision. ACTIVE is a simulation outcome, never an assertion
// that a group/instance/executor exists. Future types require separate adapters.
Operation NextOperation(Operation const& current, std::uint64_t now, bool enabled, bool participantsValid);
bool SameOperation(Operation const& a, Operation const& b);
std::vector<std::uint32_t> ParticipantGuids(Operation const& operation);
}  // namespace PlayerbotAutonomy
#endif
