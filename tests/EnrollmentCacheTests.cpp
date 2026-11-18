#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "Persistence/EnrollmentCache.h"

using namespace PlayerbotAutonomy;

int main()
{
    EnrollmentCache cache;
    assert(!cache.ShouldBlock(1));
    cache.SetEnabled(true);
    assert(cache.ShouldBlock(1));
    cache.SetReady(true);
    assert(cache.ShouldBlock(1));
    EnrollmentRecord record{1, 7, 0, 0, EnrollmentState::Absent, true, true};
    cache.Put(record);
    assert(!cache.ShouldBlock(1));
    record.State = EnrollmentState::Pending;
    cache.Put(record);
    assert(cache.ShouldBlock(1));
    record.State = EnrollmentState::Persistent;
    cache.Put(record);
    assert(cache.ShouldBlock(1));  // guild loss changes no durable state
    record.State = EnrollmentState::Retired;
    record.Guild = 4;
    cache.Put(record);
    assert(cache.ShouldBlock(1));
    record.Guild = 0;
    cache.Put(record);
    assert(!cache.ShouldBlock(1));
    record.State = EnrollmentState::Absent;
    record.Guild = 3;
    record.AccountIsRandomBot = false;
    cache.Put(record);
    assert(!cache.ShouldBlock(1));  // known human guilds are outside autonomy maintenance
    assert(!cache.ShouldBlockLegacyGuildAssignment(1));
    record.AccountIsRandomBot = true;
    record.Guild = 0;
    cache.Put(record);
    auto const oldSnapshot = cache.Version();
    cache.Invalidate(1);
    cache.PutFromSnapshot(record, oldSnapshot);
    assert(cache.ShouldBlock(1));  // guild/account mutation beats an in-flight old read
    cache.PutFromSnapshot(record, cache.Version());
    assert(!cache.ShouldBlock(1));
    cache.SetReady(false);
    assert(cache.ShouldBlock(1));
    cache.SetEnabled(false);
    assert(!cache.ShouldBlock(1));  // kill switch beats cached protection/failure
    cache.SetReady(true);
    cache.SetEnabled(true);
    cache.SetActivityEnabled(false);
    cache.Put({12, 7, 0, 1, EnrollmentState::Pending, true, true});
    assert(cache.ShouldBlock(12) && cache.ShouldBlockLegacyGuildAssignment(12));
    cache.Put({12, 7, 0, 2, EnrollmentState::Retired, true, true});
    assert(!cache.ShouldBlock(12) && !cache.ShouldBlockLegacyGuildAssignment(12));
    assert(cache.Reserve(12) && !cache.Reserve(12));
    assert(cache.LoginBlocked(12) && cache.ShouldBlock(12));
    cache.AllowLogin(12);
    assert(!cache.LoginBlocked(12) && cache.ShouldBlock(12));
    cache.Release(12);
    assert(!cache.ShouldBlock(12));
    for (std::uint32_t guid = 2; guid <= 10000; ++guid)
    {
        record.Guid = guid;
        record.State = guid % 2 ? EnrollmentState::Persistent : EnrollmentState::Absent;
        cache.Put(record);
    }
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 4; ++i)
        readers.emplace_back(
            [&cache]
            {
                for (unsigned j = 0; j < 100000; ++j)
                    assert(cache.ShouldBlock(3));
            });
    for (unsigned j = 0; j < 1000; ++j)
    {
        record.Guid = 2;
        cache.Put(record);
    }
    for (auto& reader : readers)
        reader.join();
    auto const start = std::chrono::steady_clock::now();
    unsigned blocked = 0;
    constexpr unsigned lookups = 1000000;
    for (unsigned i = 0; i < lookups; ++i)
        blocked += cache.ShouldBlock(2 + i % 9999);
    auto const ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    assert(blocked > 400000 && blocked < 600000);
    std::cout << "Cache tests passed; 10,000 records, " << lookups << " lookups, " << static_cast<double>(ns) / lookups
              << " ns/lookup (local microbenchmark)\n";
}
