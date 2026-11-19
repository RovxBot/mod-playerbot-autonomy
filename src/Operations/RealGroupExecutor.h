#ifndef PLAYERBOT_AUTONOMY_REAL_GROUP_EXECUTOR_H
#define PLAYERBOT_AUTONOMY_REAL_GROUP_EXECUTOR_H
#include "OperationCoordinator.h"
#include <set>

namespace PlayerbotAutonomy
{
struct GroupEvidence
{
    std::uint32_t Id = 0, Leader = 0;
    std::uint32_t Map = 0, Instance = 0;
    std::vector<std::uint32_t> Members;
    bool Normal = false, SafeControl = false, SafeToDisband = false;
};
class GroupPort
{
public:
    virtual ~GroupPort() = default;
    virtual bool Available() const { return true; }
    virtual std::string Admission(Operation const&) const = 0;
    virtual GroupEvidence Inspect(std::uint32_t group) const = 0;
    virtual std::uint32_t Create(std::uint32_t leader) = 0;
    virtual bool Add(Operation const& operation, RosterMember const& member) = 0;
    virtual bool Disband(std::uint32_t group) = 0;
    virtual bool ParticipantsUngrouped(Operation const&) const = 0;
};
struct GroupResult
{
    std::uint32_t Group = 0;
    bool Ready = false, Clean = false;
    std::string Reason;
};
class RealGroupExecutor
{
public:
    explicit RealGroupExecutor(GroupPort& port) : _port(port) {}
    static bool Owned(Operation const& op, GroupEvidence const& g, bool partial = false)
    {
        if (!g.Id || g.Id != op.Group || !g.Normal || !g.SafeControl || g.Leader != op.Leader || g.Members.empty() ||
            g.Members.size() > 5 || (!partial && g.Members.size() != 5))
            return false;
        std::set<std::uint32_t> expected, actual;
        for (auto const& m : op.Members)
            expected.insert(m.Guid);
        for (auto guid : g.Members)
            if (!expected.count(guid) || !actual.insert(guid).second)
                return false;
        return actual.count(op.Leader) && (partial || actual == expected);
    }
    GroupResult Assemble(Operation const& operation, bool creationAcknowledged)
    {
        GroupResult r;
        Operation op = operation;
        r.Group = op.Group;
        if (op.Execution != ExecutionMode::RealGroup || op.State != OperationState::Assembling || !op.CreationFenced ||
            op.Members.size() != 5)
        {
            r.Reason = "invalid_assembly_intent";
            return r;
        }
        if (op.Group)
        {
            r.Ready = Owned(op, _port.Inspect(op.Group));
            r.Reason = r.Ready ? "real_group_confirmed" : "group_ownership_changed";
            return r;
        }
        auto rejection = _port.Admission(op);
        if (!rejection.empty())
        {
            r.Reason = rejection;
            return r;
        }
        if (!creationAcknowledged)
        {
            r.Reason = "creation_fence_requires_reconciliation";
            return r;
        }
        op.Group = r.Group = _port.Create(op.Leader);
        if (!op.Group)
        {
            r.Reason = "core_group_create_failed";
            return r;
        }
        for (auto const& m : op.Members)
            if (m.Guid != op.Leader)
            {
                // Re-entrant hooks must not let a new owner/member be taken over.
                if (!Owned(op, _port.Inspect(op.Group), true) || !_port.Add(op, m))
                {
                    r.Reason = "partial_assembly_requires_cleanup";
                    return r;
                }
            }
        r.Ready = Owned(op, _port.Inspect(op.Group));
        r.Reason = r.Ready ? "real_group_confirmed" : "authoritative_roster_mismatch";
        return r;
    }
    GroupResult Cleanup(Operation const& op)
    {
        GroupResult r;
        r.Group = op.Group;
        auto g = _port.Inspect(op.Group);
        if (!g.Id)
        {
            r.Clean = _port.ParticipantsUngrouped(op);
            r.Reason = r.Clean ? "group_absence_confirmed" : "manual_reconciliation_unreceipted_group";
            return r;
        }
        if (!Owned(op, g, true))
        {
            r.Reason = "manual_handoff_group_ownership_changed";
            return r;
        }
        if (!g.SafeToDisband)
        {
            r.Reason = "cleanup_pending_combat_instance_binding_or_logout";
            return r;
        }
        r.Clean = _port.Disband(op.Group) && !_port.Inspect(op.Group).Id && _port.ParticipantsUngrouped(op);
        r.Reason = r.Clean ? "owned_group_cleanup_confirmed" : "core_disband_not_confirmed";
        return r;
    }

private:
    GroupPort& _port;
};
GroupPort& CoreGroups();
void RegisterCoreGroupObserver();
} // namespace PlayerbotAutonomy
#endif
