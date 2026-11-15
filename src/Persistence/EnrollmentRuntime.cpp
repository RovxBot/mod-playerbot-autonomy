#include "EnrollmentRuntime.h"

#include <limits>

#include "CharacterCache.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QueryResult.h"
#include "WorldSession.h"

#if __has_include("RoutineMaintenancePolicy.h")
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RoutineMaintenancePolicy.h"
#define AUTONOMY_HAS_MAINTENANCE_V1 1
#endif

namespace PlayerbotAutonomy
{
namespace
{
constexpr uint32 PageSize = 128;
constexpr uint32 SweepInterval = 60000;
constexpr uint32 QueryTimeout = 30000;

#ifdef AUTONOMY_HAS_MAINTENANCE_V1
bool MaintenanceProvider(uint32 guid, Playerbots::RoutineMaintenance::Action action)
{
    auto const& cache = Runtime().Cache();
    if (!cache.Enabled())
        return false;
    if (action == Playerbots::RoutineMaintenance::Action::LegacyGuildAssignment)
        return cache.ShouldBlockLegacyGuildAssignment(guid);
    if (action == Playerbots::RoutineMaintenance::Action::AccountDeletion)
        return true;
    return cache.ShouldBlock(guid);
}
#endif
}  // namespace

EnrollmentRuntime& Runtime()
{
    static EnrollmentRuntime runtime;
    return runtime;
}

void EnrollmentRuntime::Configure(bool enabled)
{
    ++_configuration;
    _cache.SetEnabled(enabled);
    _cache.SetReady(false);
    _needLoad = true;
    _cursor = 0;
    _attempted = _opened;
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
    static_assert(Playerbots::RoutineMaintenance::ContractVersion == 1, "Maintenance contract mismatch");
    if (enabled)
        Playerbots::RoutineMaintenance::RegisterProvider(&MaintenanceProvider);
    else
        Playerbots::RoutineMaintenance::UnregisterProvider(&MaintenanceProvider);
#endif
}

bool EnrollmentRuntime::ProtectionAvailable() const
{
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
    return Playerbots::RoutineMaintenance::IsProvider(&MaintenanceProvider);
#else
    return false;
#endif
}

void EnrollmentRuntime::Start(std::string const& databaseInfo)
{
    _databaseInfo = databaseInfo;
    _owner = std::this_thread::get_id();
    if (!_cache.Enabled() || !ProtectionAvailable())
    {
        LOG_INFO("module",
                 "autonomy enrollment: disabled or maintenance v1 "
                 "unavailable; enrollment unavailable");
        return;
    }
    _attempted = true;
    _opened = _store.Initialize(databaseInfo);
    if (!_opened)
    {
        Fail();
        return;
    }
    _schemaChecked = false;
    CheckSchema();
}

void EnrollmentRuntime::CheckSchema()
{
    auto callback = [this](PreparedQueryResult result)
    {
        if (!result)
            Fail();
        else
            _schemaChecked = true;
    };
    _query.emplace(_store.CheckSchema().WithPreparedCallback(std::move(callback)));
}

void EnrollmentRuntime::Stop()
{
    _cache.SetReady(false);
    _query.reset();
    _transaction.reset();
    _store.Close();
    _opened = false;
    _attempted = false;
    _schemaChecked = false;
    _request.reset();
    _orphans.clear();
    _retryIn = 0;
}

void EnrollmentRuntime::Fail()
{
    ++_counters.Failures;
    _cache.SetReady(false);
    _cursor = 0;
    _needLoad = true;
    _request.reset();
    _retryIn = 5000;
    LOG_ERROR("module",
              "autonomy enrollment: unavailable/uncertain state; "
              "maintenance remains fail-closed while enabled");
}

void EnrollmentRuntime::Update(uint32 diff)
{
    if (!OnWorldThread())
        return;
    if (_cache.Enabled() && ProtectionAvailable() && !_attempted && !_opened)
    {
        Start(_databaseInfo);
        return;
    }
    if (_query || _transaction)
    {
        _queryAge += diff;
        if (_queryAge > QueryTimeout && !_queryTimedOut)
        {
            _queryTimedOut = true;
            _cache.SetReady(false);
            ++_counters.Failures;
            LOG_ERROR("module",
                      "autonomy enrollment: DB operation timed out; "
                      "awaiting receipt, no new work");
        }
        if (_query && _query->InvokeIfReady())
            _query.reset();
        if (_transaction && _transaction->InvokeIfReady())
            _transaction.reset();
        // Callbacks may schedule the next phase; never recurse into it this tick.
        return;
    }
    _queryAge = 0;
    if (_queryTimedOut)
    {
        _queryTimedOut = false;
        _cache.SetReady(false);
        _cursor = 0;
        _needLoad = true;
    }
    if (!_cache.Enabled() || !_opened)
        return;
    if (_retryIn > diff)
    {
        _retryIn -= diff;
        return;
    }
    _retryIn = 0;
    if (!_schemaChecked)
    {
        CheckSchema();
        return;
    }
    if (_request)
    {
        LookupReceipt(true);
        return;
    }
    if (!_orphans.empty())
    {
        auto const guid = _orphans.back();
        _orphans.pop_back();
        _transaction.emplace(_store.ReconcileOrphan(guid));
        _transaction->AfterComplete(
            [this](bool success)
            {
                if (!success)
                    Fail();
            });
        return;
    }
    if (_needLoad)
    {
        LoadPage();
        return;
    }
    if (diff >= _untilSweep)
    {
        _cursor = 0;
        _needLoad = true;
        _untilSweep = SweepInterval;
    }
    else
        _untilSweep -= diff;
}

void EnrollmentRuntime::LoadPage()
{
    auto const configuration = _configuration;
    auto const version = _cache.Version();
    auto callback = [this, configuration, version](PreparedQueryResult result)
    {
        if (configuration != _configuration)
            return;
        if (!result)
        {
            Fail();
            return;
        }
        ++_counters.Pages;
        uint32 rows = 0;
        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                break;
            EnrollmentRecord record;
            record.Guid = fields[0].Get<uint32>();
            record.Account = fields[1].IsNull() ? 0 : fields[1].Get<uint32>();
            record.Guild = fields[2].Get<uint32>();
            record.State = ParseState(fields[4].Get<std::string>());
            record.Revision = fields[5].Get<uint64>();
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
            record.AccountIsRandomBot = sPlayerbotAIConfig.IsInRandomAccountList(record.Account);
#endif
            record.IdentityValid = record.Account && fields[7].IsNull() &&
                                   (fields[3].IsNull() || fields[3].Get<uint32>() == record.Account);
            bool const orphaned = record.Revision && !record.IdentityValid;
            auto const guid = ObjectGuid::Create<HighGuid::Player>(record.Guid);
            // Core guild writes use a different async pool. A stale SQL
            // snapshot must not overwrite current world-thread identity.
            record.IdentityValid = record.IdentityValid &&
                                   sCharacterCache->GetCharacterAccountIdByGuid(guid) == record.Account &&
                                   sCharacterCache->GetCharacterGuildIdByGuid(guid) == record.Guild;
            if (orphaned)
            {
                ++_counters.Orphans;
                _orphans.push_back(record.Guid);
            }
            if (record.State == EnrollmentState::Pending)
                ++_counters.Pending;
            _cache.PutFromSnapshot(record, version);
            _cursor = record.Guid;
            ++rows;
        } while (result->NextRow());
        _counters.Rows += rows;
        if (rows < PageSize)
        {
            _needLoad = false;
            _untilSweep = SweepInterval;
            _cache.SetReady(!_queryTimedOut);
            LOG_INFO("module",
                     "autonomy enrollment: reconciled snapshot; rows={}, pending "
                     "observations={}, orphan observations={}, failures={}",
                     _counters.Rows, _counters.Pending, _counters.Orphans, _counters.Failures);
        }
    };
    _query.emplace(_store.LoadPage(_cursor, PageSize).WithPreparedCallback(std::move(callback)));
}

bool EnrollmentRuntime::Validate(TransitionRequest& request, EnrollmentRecord& before)
{
    if (!OnWorldThread() || !_cache.Enabled() || !_cache.Ready() || !ProtectionAvailable() ||
        !ValidRequestId(request.RequestId) || request.Reason.empty() || request.Reason.size() > 255 ||
        request.ExpectedRevision == std::numeric_limits<uint64>::max())
        return false;
    auto const guid = ObjectGuid::Create<HighGuid::Player>(request.Guid);
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    // A veto cannot interrupt a factory that already passed its entry guard.
    // Stage only a quiescent offline identity; SQL independently requires online=0.
    if (request.Action == EnrollmentRequest::StageBeforeGuildJoin && player)
        return false;
    if (player && !player->GetSession())
        return false;
    before = _cache.Get(request.Guid);
    request.Account =
        player ? player->GetSession()->GetAccountId() : sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (!before.IdentityValid || before.Account != request.Account || before.Revision != request.ExpectedRevision ||
        sCharacterCache->GetCharacterAccountIdByGuid(guid) != request.Account)
        return false;
    EnrollmentFacts facts;
    facts.Current = before.State;
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
    facts.AccountIsRandomBot = sPlayerbotAIConfig.IsInRandomAccountList(request.Account);
    if (player)
    {
        auto* ai = GET_PLAYERBOT_AI(player);
        facts.IsSelfbot = IsSelfBot(player);
        facts.IsHumanControlled = !ai || IsRealPlayer(player) || ai->HasGameClientMaster();
    }
#else
    facts.IsHumanControlled = true;
#endif
    facts.IsAuthorized = true;  // Submit is exposed only by the console-only handler.
    facts.HasRequestId = true;
    auto const guild = player ? player->GetGuildId() : sCharacterCache->GetCharacterGuildIdByGuid(guid);
    facts.CurrentlyGuilded = guild != 0;
    facts.GuildJoinCommitted = request.Guild && guild == request.Guild;
    facts.IsGrouped = player ? (player->GetGroup() || player->GetOriginalGroup())
                             : !sCharacterCache->GetCharacterGroupGuidByGuid(guid).IsEmpty();
    // No lease/operation producer exists in M1. Adding one must extend this gate.
    return DecideEnrollmentTransition(facts, request.Action) == EnrollmentDecision::Accept;
}

bool EnrollmentRuntime::Submit(TransitionRequest request)
{
    if (!OnWorldThread())
        return false;
    if (_request || _query || _transaction || !_cache.Enabled() || !_cache.Ready() || !ProtectionAvailable() ||
        !ValidRequestId(request.RequestId))
    {
        ++_counters.Rejected;
        return false;
    }
    // Verify replay before evaluating current-state rules: a committed old STAGE
    // is still the same request after CONFIRM, and must not revert the cache.
    auto const record = _cache.Get(request.Guid);
    if (!record.IdentityValid)
        return false;
    request.Account = record.Account;
    _request = std::move(request);
    LookupReceipt(false);
    return true;
}

void EnrollmentRuntime::LookupReceipt(bool afterCommit)
{
    auto const request = *_request;
    auto callback = [this, request, afterCommit](PreparedQueryResult result)
    {
        if (!result)
        {
            Fail();
            return;
        }
        Field* fields = result->Fetch();
        if (!fields[0].IsNull())
        {
            TransitionReceipt receipt;
            receipt.Guid = fields[0].Get<uint32>();
            receipt.Account = fields[1].Get<uint32>();
            receipt.Event = fields[2].Get<std::string>();
            receipt.Outcome = ParseState(fields[4].Get<std::string>());
            receipt.Guild = fields[5].Get<uint32>();
            receipt.ActorKind = fields[6].Get<std::string>();
            receipt.ActorGuid = fields[7].Get<uint32>();
            receipt.Reason = fields[8].Get<std::string>();
            if (!fields[9].IsNull() && !fields[10].IsNull())
            {
                receipt.ExpectedRevision = fields[9].Get<uint64>();
                receipt.ResultingRevision = fields[10].Get<uint64>();
            }
            bool const matches = MatchesReceipt(request, receipt);
            if (matches)
            {
                if (afterCommit)
                    ++_counters.Committed;
                else
                    ++_counters.Replayed;
                LOG_INFO("module",
                         "autonomy enrollment: durable receipt {} character={} "
                         "event={} revision={}",
                         request.RequestId, request.Guid, EventName(request.Action), request.ExpectedRevision + 1);
            }
            else
            {
                ++_counters.Rejected;
                LOG_ERROR("module", "autonomy enrollment: conflicting request identity {}", request.RequestId);
            }
            // Read current state afresh; an old replay must never replace
            // newer enrollment.
            _cache.SetReady(false);
            _cursor = 0;
            _needLoad = true;
            _request.reset();
            return;
        }
        if (afterCommit || !_cache.Enabled() || !Validate(*_request, _before))
        {
            ++_counters.Rejected;
            _request.reset();
            if (afterCommit)
                Fail();
            return;
        }
        _cache.Invalidate(request.Guid);  // protect until commit and reload,
                                          // including uncertain outcomes
        _transaction.emplace(_store.Commit(*_request, _before));
        _transaction->AfterComplete(
            [this](bool /*success*/)
            {
                // Even a lost COMMIT acknowledgement is resolved by a durable
                // receipt. The next world tick schedules the read, never a blind
                // write retry.
                _cache.SetReady(false);
            });
    };
    _query.emplace(_store.FindReceipt(request.RequestId).WithPreparedCallback(std::move(callback)));
}
}  // namespace PlayerbotAutonomy
