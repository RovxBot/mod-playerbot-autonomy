#ifndef PLAYERBOT_AUTONOMY_OPERATION_RUNTIME_H
#define PLAYERBOT_AUTONOMY_OPERATION_RUNTIME_H
#include <optional>
#include <random>
#include <set>
#include <thread>

#include "OperationStore.h"
#include "RealGroupExecutor.h"
#include "DungeonBridge.h"
namespace PlayerbotAutonomy
{
class OperationRuntime
{
public:
    using Clock = std::uint64_t (*)();
    explicit OperationRuntime(Clock clock = nullptr, GroupPort* groups = nullptr) : _clock(clock), _groups(groups) {}
    void Configure(OperationSettings settings);
    void Start(std::string const& info);
    void Update(uint32 diff);
    void Stop();
    std::string Diagnose(uint32 guild) const;
    RosterMember Observe(uint32 guid, uint32 guild, bool managed) const;
    std::map<uint64, Operation> const& Operations() const { return _operations; }
    uint64 ScheduledJobs() const { return _queries + _writes; }
#if AUTONOMY_DC_BRIDGE
    bool Authorize(DcAutonomy::Permit const& permit, bool consume);
#endif

private:
    bool OnWorldThread() const { return std::this_thread::get_id() == _owner; }
    void Refresh();
    bool Validate(Operation const& operation, bool requireLease = true) const;
    bool ManagedGuilds(Operation const& operation, bool stopping = false) const;
    PartyPlan Plan(uint32 guild) const;
    void Read(uint64 id);
    void Save(Operation next, Operation const* before);
    void RecoverNext();
    void ReadGroup(Operation const& operation, bool absence);
    Operation AdvanceReal(Operation const& current, bool recovering = false);
    Operation CloseReal(Operation const& current);
    GroupPort& Groups() const { return _groups ? *_groups : CoreGroups(); }
    OperationSettings _settings;
    Clock _clock = nullptr;
    OperationStore _store;
    GroupPort* _groups = nullptr;
    uint64 _creationAllowed = 0, _cooldownUntil = 0, _groupCalls = 0;
    std::set<uint64> _restartGroups;
    std::set<uint64> _groupProofReady, _groupAbsenceReady;
    std::map<uint64, uint32> _createdGroups;
#if AUTONOMY_DC_BRIDGE
    std::optional<DcAutonomy::Permit> _permit;
    DcAutonomy::Status _lastDungeonStatus;
    bool _permitConsumed = false;
    uint64 _providerIdentity = 0;
#endif
    std::thread::id _owner;
    std::map<uint32, RosterMember> _roster;
    std::map<uint64, Operation> _operations;
    std::optional<Operation> _pending, _before;
    std::optional<QueryCallback> _query;
    std::optional<TransactionCallback> _transaction;
    std::mt19937_64 _ids{std::random_device{}()};
    uint64 _now = 0, _recoveryCursor = 0, _recoverId = 0, _opCursor = 0;
    uint64 _queries = 0, _writes = 0, _failures = 0, _conflicts = 0, _recoveries = 0;
    uint64 _ticks = 0, _lastTickUs = 0, _maxTickUs = 0, _age = 0;
    uint32 _refreshIn = 0, _planIn = 0, _guildCursor = 0, _planningGuild = 0;
    uint32 _retryIn = 0;
    uint64 _elapsedMs = 0;
    std::vector<uint32> _refreshGuids;
    std::size_t _refreshAt = 0;
    uint32 _refreshGuild = 0;
    bool _opened = false, _recovered = false, _readback = false;
    bool _refreshSafe = false;
    bool _refreshOverflow = false;
    bool _planTurn = false;
    std::set<uint32> _completeGuilds;
    std::string _decision = "disabled";
    uint64 _lastRealOperation = 0;
    std::string _lastRealReason = "none_since_start";
};
OperationRuntime& Operations();
}  // namespace PlayerbotAutonomy
#endif
