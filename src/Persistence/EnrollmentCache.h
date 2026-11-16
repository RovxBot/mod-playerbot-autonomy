#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_CACHE_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_CACHE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "EnrollmentState.h"

namespace PlayerbotAutonomy
{
struct EnrollmentRecord
{
    std::uint32_t Guid = 0;
    std::uint32_t Account = 0;
    std::uint32_t Guild = 0;
    std::uint64_t Revision = 0;
    EnrollmentState State = EnrollmentState::Unknown;
    bool IdentityValid = false;
    bool AccountIsRandomBot = false;
};

// The controller writes on the world thread. Map threads only copy records.
// A missing GUID is never evidence of absence, even after a complete scan.
class EnrollmentCache
{
public:
    void SetEnabled(bool enabled) { _enabled.store(enabled, std::memory_order_release); }
    bool Enabled() const { return _enabled.load(std::memory_order_acquire); }
    void SetReady(bool ready) { _ready.store(ready, std::memory_order_release); }
    bool Ready() const { return _ready.load(std::memory_order_acquire); }

    EnrollmentRecord Get(std::uint32_t guid) const
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        auto const itr = _records.find(guid);
        return itr == _records.end() ? EnrollmentRecord{} : itr->second;
    }

    void Put(EnrollmentRecord record)
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        _records[record.Guid] = record;
    }

    void Invalidate(std::uint32_t guid)
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        auto& record = _records[guid];
        record.Guid = guid;
        record.IdentityValid = false;
        _invalidations[guid] = ++_version;
    }

    std::uint64_t Version() const
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        return _version;
    }

    void PutFromSnapshot(EnrollmentRecord record, std::uint64_t version)
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        auto const changed = _invalidations.find(record.Guid);
        if (changed == _invalidations.end() || changed->second <= version)
            _records[record.Guid] = record;
    }

    bool ShouldBlock(std::uint32_t guid) const
    {
        if (!Enabled())
            return false;
        if (!Ready())
            return true;
        auto const record = Get(guid);
        if (!record.IdentityValid || record.State == EnrollmentState::Unknown ||
            record.State == EnrollmentState::Pending || record.State == EnrollmentState::Persistent)
            return true;
        return record.AccountIsRandomBot && record.Guild;
    }

    bool ShouldBlockLegacyGuildAssignment(std::uint32_t guid) const
    {
        if (!Enabled())
            return false;
        if (!guid || !Ready())
            return true;
        auto const record = Get(guid);
        return !record.IdentityValid || record.AccountIsRandomBot || record.State == EnrollmentState::Pending ||
               record.State == EnrollmentState::Persistent;
    }

private:
    std::atomic<bool> _enabled{false};
    std::atomic<bool> _ready{false};
    mutable std::shared_mutex _mutex;
    std::unordered_map<std::uint32_t, EnrollmentRecord> _records;
    std::unordered_map<std::uint32_t, std::uint64_t> _invalidations;
    std::uint64_t _version = 0;
};
}  // namespace PlayerbotAutonomy
#endif
