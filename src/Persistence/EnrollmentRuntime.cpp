#include "EnrollmentRuntime.h"

#include <cstdlib>
#include <limits>
#include <sstream>

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
#include "RandomPlayerbotMgr.h"
#include "RoutineMaintenancePolicy.h"
#define AUTONOMY_HAS_MAINTENANCE_V1 1
#endif
#if __has_include("AutonomyEnrollment.h")
#include "AutonomyEnrollment.h"
#define AUTONOMY_HAS_ENROLLMENT_V1 1
#endif

namespace PlayerbotAutonomy
{
namespace
{
constexpr uint32 PageSize = 128;
constexpr uint32 SweepInterval = 60000;
constexpr uint32 QueryTimeout = 30000;

[[noreturn]] void FailStopWithoutProtection(char const* reason)
{
    (void)reason;  // Test fake LOG_ERROR discards formatted arguments.
    // A worldserver without our veto can destroy Pending/Persistent characters.
    // Logging and continuing leaves an apparently enabled, unprotected realm.
    LOG_ERROR("module", "FATAL: playerbot autonomy cannot run without maintenance v1 protection: {}", reason);
    std::abort();
}

#ifdef AUTONOMY_HAS_MAINTENANCE_V1
[[maybe_unused]] bool EnrollmentLoginVeto(uint32 guid) { return Runtime().LoginBlocked(guid); }
bool MaintenanceProvider(uint32 guid, Playerbots::RoutineMaintenance::Action action)
{
    auto const& cache = Runtime().Cache();
    if (!cache.Enabled())
        return false;
    if (action == Playerbots::RoutineMaintenance::Action::LegacyGuildAssignment)
        return cache.ShouldBlockLegacyGuildAssignment(guid);
    if (action == Playerbots::RoutineMaintenance::Action::AccountDeletion)
        return cache.MayContainProtected();
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
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
    static_assert(Playerbots::RoutineMaintenance::ContractVersion == 1, "Maintenance contract mismatch");
    if (!Playerbots::RoutineMaintenance::RegisterProvider(&MaintenanceProvider))
        FailStopWithoutProtection("maintenance provider already owned by another module");
#else
    (void)enabled;
    FailStopWithoutProtection("required playerbots interface was absent at compilation");
#endif
#ifdef AUTONOMY_HAS_ENROLLMENT_V1
    static_assert(Playerbots::AutonomyEnrollment::ContractVersion == 1, "Enrollment contract mismatch");
    if (!Playerbots::AutonomyEnrollment::Register(&EnrollmentLoginVeto))
        FailStopWithoutProtection("enrollment login provider already owned by another module");
#endif
    _activityEnabled = enabled;
    _cache.SetActivityEnabled(enabled);
    ++_configuration;
    _cache.SetEnabled(true);
    _cache.SetReady(false);
    _needLoad = true;
    _cursor = 0;
    _attempted = _opened;
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
    if (!_cache.Enabled())
        return;
    if (!ProtectionAvailable())
        FailStopWithoutProtection("enabled startup has no registered maintenance provider");
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
    Release(_cache.Reserved());
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
    if (_cache.Reserved())
    {
        _reservationAge += diff;
        if (_reservationAge >= 120000 && !_request && !_transaction)
            Release(_cache.Reserved());  // Durable Pending/unknown protection survives transient expiry.
    }
    if (_cache.Enabled() && !ProtectionAvailable())
        FailStopWithoutProtection("maintenance provider was lost while autonomy remained enabled");
    if (_cache.Enabled() && !_attempted && !_opened)
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
            record.AccountIsRandomBot = sRandomPlayerbotMgr.IsDedicatedRandomAccount(record.Account);
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

namespace
{
void ValidateDiagnosis(EnrollmentDiagnosis& diagnosis, TransitionRequest const& request, bool checkEnvelope)
{
    auto check = [&]() -> EnrollmentValidation
    {
        if (!diagnosis.WorldThread)
            return EnrollmentValidation::WrongThread;
        if (!diagnosis.Enabled)
            return EnrollmentValidation::Disabled;
        if (!diagnosis.Ready)
            return EnrollmentValidation::CacheNotReady;
        if (!diagnosis.MaintenanceAvailable)
            return EnrollmentValidation::MissingMaintenance;
        if (checkEnvelope && !ValidRequestId(request.RequestId))
            return EnrollmentValidation::InvalidRequestId;
        if (checkEnvelope && (request.Reason.empty() || request.Reason.size() > 255))
            return EnrollmentValidation::InvalidReason;
        if (request.ExpectedRevision == std::numeric_limits<uint64>::max())
            return EnrollmentValidation::RevisionOverflow;
        // A veto cannot interrupt a factory that already passed its entry guard.
        // SQL independently requires online=0, even if no Player is connected.
        if (request.Action == EnrollmentRequest::StageBeforeGuildJoin && diagnosis.ConnectedPlayer)
            return EnrollmentValidation::StageRequiresOffline;
        if (diagnosis.ConnectedPlayer && !diagnosis.SessionPresent)
            return EnrollmentValidation::MissingSession;
        if (!diagnosis.CacheFound)
            return EnrollmentValidation::CacheMiss;
        if (!diagnosis.Cached.IdentityValid)
            return EnrollmentValidation::InvalidCacheIdentity;
        if (diagnosis.Cached.Account != diagnosis.Account)
            return EnrollmentValidation::CachedAccountMismatch;
        if (diagnosis.Cached.Revision != request.ExpectedRevision)
            return EnrollmentValidation::RevisionMismatch;
        if (diagnosis.CharacterAccount != diagnosis.Account)
            return EnrollmentValidation::CharacterAccountMismatch;
        diagnosis.Facts.GuildJoinCommitted = request.Guild && diagnosis.Guild == request.Guild;
        // No lease/operation producer exists in M1. Adding one must extend this gate.
        diagnosis.Decision = DecideEnrollmentTransition(diagnosis.Facts, request.Action);
        return diagnosis.Decision == EnrollmentDecision::Accept ? EnrollmentValidation::Accept
                                                                : EnrollmentValidation::PolicyRejected;
    };
    diagnosis.Validation = check();
}
}  // namespace

char const* EnrollmentDiagnosis::Reason() const
{
    switch (Validation)
    {
        case EnrollmentValidation::Accept:
            return "accept";
        case EnrollmentValidation::WrongThread:
            return "wrong_world_thread";
        case EnrollmentValidation::Disabled:
            return "autonomy_disabled";
        case EnrollmentValidation::CacheNotReady:
            return "cache_not_ready";
        case EnrollmentValidation::MissingMaintenance:
            return "maintenance_v1_unavailable";
        case EnrollmentValidation::InvalidRequestId:
            return "invalid_request_id";
        case EnrollmentValidation::InvalidReason:
            return "invalid_reason";
        case EnrollmentValidation::RevisionOverflow:
            return "revision_overflow";
        case EnrollmentValidation::StageRequiresOffline:
            return "stage_requires_offline";
        case EnrollmentValidation::MissingSession:
            return "connected_player_missing_session";
        case EnrollmentValidation::CacheMiss:
            return "autonomy_cache_miss";
        case EnrollmentValidation::InvalidCacheIdentity:
            return "cache_identity_invalid";
        case EnrollmentValidation::CachedAccountMismatch:
            return "cached_account_mismatch";
        case EnrollmentValidation::RevisionMismatch:
            return "revision_mismatch";
        case EnrollmentValidation::CharacterAccountMismatch:
            return "character_cache_account_mismatch";
        case EnrollmentValidation::PolicyRejected:
            break;
    }
    switch (Decision)
    {
        case EnrollmentDecision::AlreadyInRequestedState:
            return "already_in_requested_state";
        case EnrollmentDecision::DenyUnknownState:
            return "unknown_enrollment_state";
        case EnrollmentDecision::DenyNonRandomAccount:
            return "non_random_account";
        case EnrollmentDecision::DenyHumanControl:
            return "human_or_selfbot_control";
        case EnrollmentDecision::DenyMissingAuthorization:
            return "missing_authorization";
        case EnrollmentDecision::DenyMissingRequestId:
            return "missing_request_id";
        case EnrollmentDecision::DenyMissingConfirmedGuild:
            return "guild_join_not_confirmed";
        case EnrollmentDecision::DenyNoPendingEnrollment:
            return "no_pending_enrollment";
        case EnrollmentDecision::DenyActivityInProgress:
            return "activity_in_progress";
        case EnrollmentDecision::DenyStillGuilded:
            return "still_guilded";
        case EnrollmentDecision::DenyNeverEnrolled:
            return "never_enrolled";
        case EnrollmentDecision::Accept:
            break;
    }
    return "validation_rejected";
}

EnrollmentDiagnosis EnrollmentRuntime::Observe(uint32 lowGuid, EnrollmentRequest action) const
{
    EnrollmentDiagnosis diagnosis;
    diagnosis.Guid = lowGuid;
    diagnosis.WorldThread = OnWorldThread();
    if (!diagnosis.WorldThread)
        return diagnosis;  // Never read thread-affine engine state from a foreign thread.
    diagnosis.Enabled = _activityEnabled;
    diagnosis.Ready = _cache.Ready();
    diagnosis.MaintenanceAvailable = ProtectionAvailable();
    diagnosis.Cached = _cache.Get(lowGuid);
    // Cache records always carry their key, including invalidated placeholders.
    diagnosis.CacheFound = lowGuid && diagnosis.Cached.Guid == lowGuid;
    auto const guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
    diagnosis.CharacterAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    diagnosis.CharacterGuild = sCharacterCache->GetCharacterGuildIdByGuid(guid);
    auto const group = sCharacterCache->GetCharacterGroupGuidByGuid(guid);
    diagnosis.CharacterGroupGuid = group.GetRawValue();
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    diagnosis.ConnectedPlayer = player != nullptr;
    WorldSession* session = player ? player->GetSession() : nullptr;
    diagnosis.SessionPresent = session != nullptr;
    diagnosis.Account = player ? (session ? session->GetAccountId() : 0) : diagnosis.CharacterAccount;
    bool const readPlayerFacts = player && session && action != EnrollmentRequest::StageBeforeGuildJoin;
    diagnosis.Guild = readPlayerFacts ? player->GetGuildId() : diagnosis.CharacterGuild;
    diagnosis.Facts.Current = diagnosis.Cached.State;
#ifdef AUTONOMY_HAS_MAINTENANCE_V1
    diagnosis.FactoryRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(diagnosis.Account);
    diagnosis.RandomAccountChecked = true;
    diagnosis.Facts.AccountIsRandomBot = sRandomPlayerbotMgr.IsDedicatedRandomAccount(diagnosis.Account);
    if (readPlayerFacts)
    {
        auto* ai = GET_PLAYERBOT_AI(player);
        diagnosis.Facts.IsSelfbot = IsSelfBot(player);
        diagnosis.Facts.IsHumanControlled = !ai || IsRealPlayer(player) || ai->HasGameClientMaster();
    }
#else
    diagnosis.Facts.IsHumanControlled = true;
#endif
    // These read-only methods do not grant authority. The console-only handler
    // authorizes Submit; request identity is checked separately for real requests.
    diagnosis.Facts.IsAuthorized = true;
    diagnosis.Facts.HasRequestId = true;
    diagnosis.Facts.CurrentlyGuilded = diagnosis.Guild != 0;
    diagnosis.Facts.IsGrouped = readPlayerFacts ? (player->GetGroup() || player->GetOriginalGroup()) : !group.IsEmpty();
    return diagnosis;
}

EnrollmentDiagnosis EnrollmentRuntime::Diagnose(TransitionRequest const& request) const
{
    auto diagnosis = Observe(request.Guid, request.Action);
    ValidateDiagnosis(diagnosis, request, true);
    return diagnosis;
}

EnrollmentDiagnosis EnrollmentRuntime::DiagnoseStage(uint32 guid) const
{
    auto diagnosis = Observe(guid, EnrollmentRequest::StageBeforeGuildJoin);
    TransitionRequest eligibility;
    eligibility.Guid = guid;
    eligibility.ExpectedRevision = diagnosis.Cached.Revision;
    ValidateDiagnosis(diagnosis, eligibility, false);
    return diagnosis;
}

std::string FormatStageDiagnosis(EnrollmentDiagnosis const& diagnosis)
{
    std::ostringstream output;
    output << std::boolalpha << "Autonomy diagnose guid=" << diagnosis.Guid
           << " world_thread=" << diagnosis.WorldThread;
    if (!diagnosis.WorldThread)
        return output.str() + " stage_world_checks=FAIL reason=" + diagnosis.Reason() + " facts=unobserved";
    output << " enabled=" << diagnosis.Enabled << " maintenance_v1=" << diagnosis.MaintenanceAvailable
           << " ready=" << diagnosis.Ready << "\nautonomy_cache_found=" << diagnosis.CacheFound
           << " identity_valid=" << diagnosis.Cached.IdentityValid << " cached_account=" << diagnosis.Cached.Account
           << " cached_guild=" << diagnosis.Cached.Guild << " cached_state=" << StateName(diagnosis.Cached.State)
           << " cached_revision=" << diagnosis.Cached.Revision << "\nrandom_account="
           << (diagnosis.RandomAccountChecked ? (diagnosis.Facts.AccountIsRandomBot ? "true" : "false") : "unavailable")
           << " factory_account=" << diagnosis.FactoryRandomAccount
           << " dedicated_type1_account=" << diagnosis.Facts.AccountIsRandomBot
           << " validation_account=" << diagnosis.Account << " character_cache_account=" << diagnosis.CharacterAccount
           << " character_cache_guild=" << diagnosis.CharacterGuild
           << " character_cache_group_guid=" << diagnosis.CharacterGroupGuid
           << "\nconnected_player=" << diagnosis.ConnectedPlayer << " world_session="
           << (diagnosis.ConnectedPlayer ? (diagnosis.SessionPresent ? "present" : "absent") : "not_applicable")
           << "\nstage_world_checks=" << (diagnosis.Passed() ? "PASS" : "FAIL") << " reason=" << diagnosis.Reason()
           << " expected_revision=" << diagnosis.Cached.Revision
           << " scope=current_world_eligibility_only; request/replay/busy/SQL checks not evaluated; nothing queued";
    return output.str();
}

bool EnrollmentRuntime::Submit(TransitionRequest request)
{
    if (!OnWorldThread())
        return false;
    if (_request || _query || _transaction || !_activityEnabled || !_cache.Ready() || !ProtectionAvailable() ||
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
        // A missing post-commit receipt is a DB failure, not a fresh validation
        // attempt. Never retry the write or read engine state on that path.
        auto const diagnosis = afterCommit ? EnrollmentDiagnosis{} : Diagnose(*_request);
        if (afterCommit || !diagnosis.Passed())
        {
            LOG_ERROR("module", "autonomy enrollment: rejected request={} character={} event={} reason={}",
                      request.RequestId, request.Guid, EventName(request.Action),
                      afterCommit ? "missing_durable_receipt" : diagnosis.Reason());
            ++_counters.Rejected;
            _request.reset();
            if (afterCommit)
                Fail();
            return;
        }
        _request->Account = diagnosis.Account;
        _before = diagnosis.Cached;
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

namespace PlayerbotAutonomy
{
bool EnrollmentRuntime::Reserve(uint32 guid)
{
#ifndef AUTONOMY_HAS_ENROLLMENT_V1
    (void)guid;
    return false;
#else
    if (!OnWorldThread() || !_activityEnabled || !Idle() ||
        !Playerbots::AutonomyEnrollment::IsOwner(&EnrollmentLoginVeto))
        return false;
    auto const record = _cache.Get(guid);
    if (!record.IdentityValid || !record.AccountIsRandomBot || record.Guild ||
        record.State == EnrollmentState::Unknown || record.State == EnrollmentState::Persistent)
        return false;
    _reservationAge = 0;
    return _cache.Reserve(guid);
#endif
}
void EnrollmentRuntime::Release(uint32 guid)
{
    if (OnWorldThread())
        _cache.Release(guid);
}
bool EnrollmentRuntime::PrepareOffline(uint32 lowGuid)
{
#if defined(AUTONOMY_HAS_ENROLLMENT_V1) && defined(AUTONOMY_HAS_MAINTENANCE_V1)
    if (!OnWorldThread() || _cache.Reserved() != lowGuid || !_cache.LoginBlocked(lowGuid))
        return false;
    auto guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
    auto* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player)
        return !PlayerbotHolder::HasPendingBotLogin(guid);
    auto* ai = GET_PLAYERBOT_AI(player);
    if (!player->GetSession() || !ai || IsRealPlayer(player) || IsSelfBot(player) || ai->HasGameClientMaster() ||
        player->GetGuildId() || player->GetGroup() || player->GetOriginalGroup() || player->IsInCombat() ||
        player->IsBeingTeleported() || !player->IsAlive() || sRandomPlayerbotMgr.GetPlayerBot(guid) != player)
        return false;
    sRandomPlayerbotMgr.LogoutPlayerBot(guid);
    return false;  // Re-resolve only on a later tick, after normal logout/save and pending-login cleanup.
#else
    (void)lowGuid;
    return false;
#endif
}
bool EnrollmentRuntime::HandoffLogin(uint32 lowGuid)
{
#if defined(AUTONOMY_HAS_ENROLLMENT_V1) && defined(AUTONOMY_HAS_MAINTENANCE_V1)
    if (!OnWorldThread() || !_activityEnabled || !Idle() || _cache.Reserved() != lowGuid)
        return false;
    auto const record = _cache.Get(lowGuid);
    if (!record.IdentityValid || record.State != EnrollmentState::Pending || !record.AccountIsRandomBot)
        return false;
    _cache.AllowLogin(lowGuid);
    sRandomPlayerbotMgr.AddPlayerBot(ObjectGuid::Create<HighGuid::Player>(lowGuid), 0);
    return true;
#else
    (void)lowGuid;
    return false;
#endif
}
}  // namespace PlayerbotAutonomy
