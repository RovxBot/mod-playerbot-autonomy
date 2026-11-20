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
enum class ExecutionMode : std::uint8_t { Simulated, RealGroup };
char const* OperationStateName(OperationState state);
bool Terminal(OperationState state);
bool CanTransition(OperationState from, OperationState to);
struct Operation
{
    std::uint64_t Id = 0, Revision = 0, CreatedAt = 0, ChangedAt = 0, Deadline = 0, LeaseUntil = 0;
    std::uint32_t Guild = 0, Leader = 0, Faction = 2, Attempts = 0;
    OperationType Type = OperationType::DungeonParty;
    OperationState State = OperationState::Proposed;
    ExecutionMode Execution = ExecutionMode::Simulated;
    std::uint32_t Group = 0;
    bool CreationFenced = false, Succeeded = false, GroupConfirmed = false;
    std::vector<RosterMember> Members;
    std::string Reason = "simulated_dungeon_party";
};
struct OperationSettings
{
    bool Enabled = false;
    bool DryRun = true;           // Refresh/plan only; no new operation writes or leases.
    bool AssembleGroups = false;
    bool TrustedDungeon = false, KillSwitch = false;
    std::uint32_t PilotProfile = 0, PilotGuild = 0, RealmMaxExpansion = 0;
    std::uint32_t MaxReal = 1, AssemblySeconds = 90, ProgressSeconds = 600, RunSeconds = 3600;
    std::uint32_t CooldownSeconds = 300;
    std::uint32_t MaxConcurrent = 1, IntervalMs = 5000, RefreshBudget = 4;
    std::uint32_t LeaseSeconds = 120, TimeoutSeconds = 90;
    RosterRules Rules;
    bool Valid() const;
};
// Simulation only. Real operations require authoritative executor evidence.
Operation NextOperation(Operation const& current, std::uint64_t now, bool enabled, bool participantsValid);
bool SameOperation(Operation const& a, Operation const& b);
std::vector<std::uint32_t> ParticipantGuids(Operation const& operation);
}  // namespace PlayerbotAutonomy
#endif
