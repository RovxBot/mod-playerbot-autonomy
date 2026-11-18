#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_RUNTIME_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_RUNTIME_H

#include <optional>
#include <thread>
#include <vector>

#include "EnrollmentStore.h"

namespace PlayerbotAutonomy
{
struct RuntimeCounters
{
    uint64 Pages = 0;
    uint64 Rows = 0;
    uint64 Pending = 0;
    uint64 Orphans = 0;
    uint64 Failures = 0;
    uint64 Committed = 0;
    uint64 Replayed = 0;
    uint64 Rejected = 0;
    uint64 ExpiredReservations = 0;
};

enum class EnrollmentValidation
{
    Accept,
    WrongThread,
    Disabled,
    CacheNotReady,
    MissingMaintenance,
    InvalidRequestId,
    InvalidReason,
    RevisionOverflow,
    StageRequiresOffline,
    MissingSession,
    CacheMiss,
    InvalidCacheIdentity,
    CachedAccountMismatch,
    RevisionMismatch,
    CharacterAccountMismatch,
    PolicyRejected
};

// A value snapshot only: no engine pointers, SQL, invalidation or submission.
struct EnrollmentDiagnosis
{
    uint32 Guid = 0;
    bool WorldThread = false;
    bool Enabled = false;
    bool Ready = false;
    bool MaintenanceAvailable = false;
    bool CacheFound = false;
    EnrollmentRecord Cached;
    uint32 CharacterAccount = 0;
    uint32 CharacterGuild = 0;
    uint64 CharacterGroupGuid = 0;
    bool ConnectedPlayer = false;
    bool SessionPresent = false;
    bool FactoryRandomAccount = false;
    bool RandomAccountChecked = false;
    uint32 Account = 0;
    uint32 Guild = 0;
    EnrollmentFacts Facts;
    EnrollmentValidation Validation = EnrollmentValidation::WrongThread;
    EnrollmentDecision Decision = EnrollmentDecision::DenyUnknownState;

    bool Passed() const { return Validation == EnrollmentValidation::Accept; }
    char const* Reason() const;
};

std::string FormatStageDiagnosis(EnrollmentDiagnosis const& diagnosis);

class EnrollmentRuntime
{
public:
    // Activity can stop; protection is a process-lifetime requirement.
    void Configure(bool enabled);
    bool ActivityEnabled() const { return _activityEnabled; }
    bool Idle() const { return !_request && !_query && !_transaction && _cache.Ready(); }
    bool Reserve(uint32 guid);
    bool PrepareOffline(uint32 guid);
    bool HandoffLogin(uint32 guid);
    void Release(uint32 guid);
    bool LoginBlocked(uint32 guid) const { return _cache.LoginBlocked(guid); }
    void Start(std::string const& databaseInfo);
    void Update(uint32 diff);
    void Stop();
    bool Submit(TransitionRequest request);
    void Invalidate(uint32 guid)
    {
        _cache.Invalidate(guid);
        if (OnWorldThread())
        {
            _cursor = 0;
            _needLoad = true;
        }
    }
    EnrollmentCache& Cache() { return _cache; }
    RuntimeCounters const& Counters() const { return _counters; }
    bool ProtectionAvailable() const;
    EnrollmentDiagnosis DiagnoseStage(uint32 guid) const;
    EnrollmentDiagnosis Diagnose(TransitionRequest const& request) const;

private:
    EnrollmentDiagnosis Observe(uint32 guid, EnrollmentRequest action) const;
    void LoadPage();
    void CheckSchema();
    void LookupReceipt(bool afterCommit);
    void Fail();
    bool OnWorldThread() const { return std::this_thread::get_id() == _owner; }

    EnrollmentCache _cache;
    EnrollmentStore _store;
    RuntimeCounters _counters;
    std::thread::id _owner;
    bool _activityEnabled = false;
    uint32 _reservationAge = 0;
    bool _opened = false;
    bool _attempted = false;
    bool _schemaChecked = false;
    bool _needLoad = true;
    uint64 _configuration = 0;
    uint32 _cursor = 0;
    uint32 _untilSweep = 0;
    uint32 _queryAge = 0;
    uint32 _retryIn = 0;
    bool _queryTimedOut = false;
    std::optional<QueryCallback> _query;
    std::optional<TransactionCallback> _transaction;
    std::optional<TransitionRequest> _request;
    EnrollmentRecord _before;
    std::vector<uint32> _orphans;
    std::string _databaseInfo;
};

EnrollmentRuntime& Runtime();
}  // namespace PlayerbotAutonomy
#endif
