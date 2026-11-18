#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_STATE_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_STATE_H

#include <cstdint>

namespace PlayerbotAutonomy
{
// Durable enrollment is distinct from current guild membership and from the
// transient playerbots "add" / currentBots population event. Pending,
// Persistent and Retired are stored as rows; Absent is a CONFIRMED empty
// lookup and Unknown is unavailable/unloaded data. A missing DB is NOT Absent.
// Pending is a durable pre-join protection, NOT permission to recruit.
enum class EnrollmentState
{
    Unknown,
    Absent,
    Pending,
    Persistent,
    Retired
};

enum class EnrollmentRequest
{
    StageBeforeGuildJoin,
    ConfirmAfterGuildJoin,
    CancelFailedGuildJoin,
    ExplicitRetirement
};

// Absent has no durable row; every stored lifecycle starts at revision one.
// Unknown or inconsistent SQL snapshots must never authorize maintenance/work.
constexpr bool ValidEnrollmentRevision(EnrollmentState state, std::uint64_t revision)
{
    if (state == EnrollmentState::Absent)
        return revision == 0;
    return (state == EnrollmentState::Pending || state == EnrollmentState::Persistent ||
            state == EnrollmentState::Retired) && revision > 0;
}

enum class EnrollmentDecision
{
    Accept,
    AlreadyInRequestedState,
    DenyUnknownState,
    DenyNonRandomAccount,
    DenyHumanControl,
    DenyMissingAuthorization,
    DenyMissingRequestId,
    DenyMissingConfirmedGuild,
    DenyNoPendingEnrollment,
    DenyActivityInProgress,
    DenyStillGuilded,
    DenyNeverEnrolled
};

struct EnrollmentFacts
{
    EnrollmentState Current = EnrollmentState::Unknown;
    bool AccountIsRandomBot = false; // authoritative account list, not currentBots
    bool IsSelfbot = false;
    bool IsHumanControlled = false;
    bool IsAuthorized = false;      // independently verified actor/capability
    bool HasRequestId = false;      // durable idempotency key
    bool GuildJoinCommitted = false; // authoritative guild row already committed
    bool CurrentlyGuilded = false;
    bool HasActiveLease = false;
    bool HasActiveOperation = false;
    bool IsGrouped = false;
};

// Pure, side-effect-free transition rules. The future adapter must revalidate
// actor/account/guild/lease facts inside an atomic DB update before publishing
// any cache change. This function DOES NOT access the DB or enroll a bot.
constexpr EnrollmentDecision DecideEnrollmentTransition(EnrollmentFacts const& facts,
                                                        EnrollmentRequest request)
{
    if (facts.Current == EnrollmentState::Unknown)
        return EnrollmentDecision::DenyUnknownState;
    if (!facts.AccountIsRandomBot)
        return EnrollmentDecision::DenyNonRandomAccount;
    if (facts.IsSelfbot || facts.IsHumanControlled)
        return EnrollmentDecision::DenyHumanControl;
    if (!facts.IsAuthorized)
        return EnrollmentDecision::DenyMissingAuthorization;
    if (!facts.HasRequestId)
        return EnrollmentDecision::DenyMissingRequestId;

    if (request == EnrollmentRequest::StageBeforeGuildJoin)
    {
        if (facts.Current == EnrollmentState::Pending ||
            facts.Current == EnrollmentState::Persistent)
            return EnrollmentDecision::AlreadyInRequestedState;

        if (facts.HasActiveLease || facts.HasActiveOperation || facts.IsGrouped)
            return EnrollmentDecision::DenyActivityInProgress;
        // Stage is written BEFORE the guild change, or for authorized
        // adoption of an existing guild member. It does not grant items, XP,
        // guild ownership or permission to bypass the guild API.
        return EnrollmentDecision::Accept;
    }

    if (request == EnrollmentRequest::ConfirmAfterGuildJoin)
    {
        if (facts.Current == EnrollmentState::Persistent)
            return EnrollmentDecision::AlreadyInRequestedState;
        if (facts.Current != EnrollmentState::Pending)
            return EnrollmentDecision::DenyNoPendingEnrollment;
        if (!facts.GuildJoinCommitted || !facts.CurrentlyGuilded)
            return EnrollmentDecision::DenyMissingConfirmedGuild;
        return EnrollmentDecision::Accept;
    }

    if (request == EnrollmentRequest::CancelFailedGuildJoin)
    {
        if (facts.Current == EnrollmentState::Retired)
            return EnrollmentDecision::AlreadyInRequestedState;
        if (facts.Current != EnrollmentState::Pending)
            return EnrollmentDecision::DenyNoPendingEnrollment;
        // Never release a pending protection record while the guild may
        // already contain the bot or an operation/lease is still active.
        if (facts.CurrentlyGuilded)
            return EnrollmentDecision::DenyStillGuilded;
        if (facts.HasActiveLease || facts.HasActiveOperation || facts.IsGrouped)
            return EnrollmentDecision::DenyActivityInProgress;
        // A cancelled, never-completed reservation becomes a retired
        // tombstone (resettable filler), NOT a fabricated character reward.
        return EnrollmentDecision::Accept;
    }

    if (facts.Current == EnrollmentState::Retired)
        return EnrollmentDecision::AlreadyInRequestedState;
    if (facts.Current == EnrollmentState::Absent)
        return EnrollmentDecision::DenyNeverEnrolled;
    if (facts.Current == EnrollmentState::Pending)
        return EnrollmentDecision::DenyNoPendingEnrollment;

    // A retire-and-reroll must not occur while a character is still a guild
    // member or part of an operation/group. Require a separately authorized
    // and completed guild leave / disband before retirement is committed.
    if (facts.CurrentlyGuilded)
        return EnrollmentDecision::DenyStillGuilded;
    if (facts.HasActiveLease || facts.HasActiveOperation || facts.IsGrouped)
        return EnrollmentDecision::DenyActivityInProgress;
    return EnrollmentDecision::Accept;
}

constexpr EnrollmentState StateAfterAcceptedTransition(EnrollmentRequest request)
{
    if (request == EnrollmentRequest::StageBeforeGuildJoin)
        return EnrollmentState::Pending;
    if (request == EnrollmentRequest::ConfirmAfterGuildJoin)
        return EnrollmentState::Persistent;
    return EnrollmentState::Retired;
}

// A guild departure is not a retirement event. Neither login nor an empty
// random-bot "add" event may cause a lifecycle transition.
constexpr EnrollmentState StateAfterGuildChange(EnrollmentState state, bool /*isGuilded*/)
{
    return state;
}

// Intended future cache query, not wired to the game engine. While autonomy
// is enabled an unavailable enrollment snapshot must not be misclassified as
// resettable world filler. Other hard eligibility and authorization checks
// must still run. Disabled autonomy always defers to legacy playerbots.
constexpr bool MustProtectFromDestructiveMaintenance(bool enabled, bool accountIsRandomBot,
                                                     bool isSelfbot, bool hasHumanController,
                                                     EnrollmentState state, bool currentlyGuilded,
                                                     bool hasExclusiveLease)
{
    // Durable ownership outlives activity switches and account/control changes.
    if (state == EnrollmentState::Pending || state == EnrollmentState::Persistent)
        return true;
    if (!enabled || !accountIsRandomBot || isSelfbot || hasHumanController)
        return false;
    return state == EnrollmentState::Unknown || state == EnrollmentState::Pending ||
        state == EnrollmentState::Persistent || currentlyGuilded || hasExclusiveLease;
}

} // namespace PlayerbotAutonomy

#endif // PLAYERBOT_AUTONOMY_ENROLLMENT_STATE_H
