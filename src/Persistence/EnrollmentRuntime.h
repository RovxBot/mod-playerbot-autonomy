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
};

class EnrollmentRuntime
{
public:
    void Configure(bool enabled);
    void Start(std::string const& databaseInfo);
    void Update(uint32 diff);
    void Stop();
    bool Submit(TransitionRequest request);
    void Invalidate(uint32 guid) { _cache.Invalidate(guid); }
    EnrollmentCache& Cache() { return _cache; }
    RuntimeCounters const& Counters() const { return _counters; }
    bool ProtectionAvailable() const;

private:
    bool Validate(TransitionRequest& request, EnrollmentRecord& before);
    void LoadPage();
    void CheckSchema();
    void LookupReceipt(bool afterCommit);
    void Fail();
    bool OnWorldThread() const { return std::this_thread::get_id() == _owner; }

    EnrollmentCache _cache;
    EnrollmentStore _store;
    RuntimeCounters _counters;
    std::thread::id _owner;
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
