#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_CACHE_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_CACHE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
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
        _keys.insert(record.Guid);
    }

    std::uint32_t NextGuid(std::uint32_t cursor) const
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        auto next = _keys.upper_bound(cursor);
        return next == _keys.end() ? 0 : *next;
    }

    void Invalidate(std::uint32_t guid)
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        auto& record = _records[guid];
        record.Guid = guid;
        record.IdentityValid = false;
        _invalidations[guid] = ++_version;
        _dirty.insert(guid);
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
        {
            _records[record.Guid] = record;
            _keys.insert(record.Guid);
            if (record.IdentityValid)
                _dirty.erase(record.Guid);
            else
                _dirty.insert(record.Guid);
        }
    }

    std::uint32_t NextDirty(std::uint32_t cursor) const
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        auto next = _dirty.upper_bound(cursor);
        return next == _dirty.end() ? 0 : *next;
    }
    std::size_t DirtyCount() const
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        return _dirty.size();
    }

    void SetActivityEnabled(bool enabled) { _activity.store(enabled, std::memory_order_release); }
    bool Reserve(std::uint32_t guid)
    {
        std::uint32_t expected = 0;
        if (!guid || !_reserved.compare_exchange_strong(expected, guid))
            return false;
        _loginBlocked.store(guid, std::memory_order_release);
        return true;
    }
    std::uint32_t Reserved() const { return _reserved.load(std::memory_order_acquire); }
    bool LoginBlocked(std::uint32_t guid) const
    {
        return guid && _loginBlocked.load(std::memory_order_acquire) == guid;
    }
    void AllowLogin(std::uint32_t guid)
    {
        if (Reserved() == guid)
            _loginBlocked.store(0, std::memory_order_release);
    }
    void Release(std::uint32_t guid)
    {
        if (Reserved() == guid)
        {
            _loginBlocked.store(0, std::memory_order_release);
            _reserved.store(0, std::memory_order_release);
        }
    }

    bool ShouldBlock(std::uint32_t guid) const
    {
        if (!Enabled())
            return false;
        if (Reserved() == guid || !Ready())
            return true;
        auto const record = Get(guid);
        if (!record.IdentityValid || record.State == EnrollmentState::Unknown ||
            record.State == EnrollmentState::Pending || record.State == EnrollmentState::Persistent)
            return true;
        return record.AccountIsRandomBot && record.Guild;
    }

    // Bulk deletion is rare and world-thread-only. Never put this scan on bot ticks.
    bool MayContainProtected() const
    {
        if (!Ready() || Reserved())
            return true;
        std::shared_lock<std::shared_mutex> lock(_mutex);
        for (auto const& item : _records)
        {
            auto const& r = item.second;
            if (!r.IdentityValid || r.State == EnrollmentState::Pending || r.State == EnrollmentState::Persistent ||
                (r.AccountIsRandomBot && r.Guild))
                return true;
        }
        return false;
    }

    bool ShouldBlockLegacyGuildAssignment(std::uint32_t guid) const
    {
        if (!Enabled())
            return false;
        if (!guid)
            return _activity.load(std::memory_order_acquire) || MayContainProtected();
        if (!Ready())
            return true;
        auto const record = Get(guid);
        return Reserved() == guid || !record.IdentityValid ||
               (_activity.load(std::memory_order_acquire) && record.AccountIsRandomBot) ||
               record.State == EnrollmentState::Pending || record.State == EnrollmentState::Persistent;
    }

private:
    std::atomic<bool> _enabled{false};
    std::atomic<bool> _activity{false};
    std::atomic<std::uint32_t> _reserved{0};
    std::atomic<std::uint32_t> _loginBlocked{0};
    std::atomic<bool> _ready{false};
    mutable std::shared_mutex _mutex;
    std::set<std::uint32_t> _keys;
    std::set<std::uint32_t> _dirty;
    std::unordered_map<std::uint32_t, EnrollmentRecord> _records;
    std::unordered_map<std::uint32_t, std::uint64_t> _invalidations;
    std::uint64_t _version = 0;
};
}  // namespace PlayerbotAutonomy
#endif
