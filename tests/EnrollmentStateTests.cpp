#include "Persistence/EnrollmentState.h"

#include <cassert>

using namespace PlayerbotAutonomy;

namespace
{
constexpr EnrollmentFacts ValidCandidate(EnrollmentState state)
{
    EnrollmentFacts facts;
    facts.Current = state;
    facts.AccountIsRandomBot = true;
    facts.IsAuthorized = true;
    facts.HasRequestId = true;
    return facts;
}

constexpr bool TestTwoPhaseEnrollment()
{
    EnrollmentFacts facts = ValidCandidate(EnrollmentState::Absent);

    // Protection is made durable before issuing the guild API call.
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::Accept)
        return false;
    facts.Current = StateAfterAcceptedTransition(EnrollmentRequest::StageBeforeGuildJoin);
    if (facts.Current != EnrollmentState::Pending)
        return false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::AlreadyInRequestedState)
        return false;

    // A crash while the guild write is in flight must not remove protection.
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, facts.Current, false, false))
        return false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ConfirmAfterGuildJoin) !=
        EnrollmentDecision::DenyMissingConfirmedGuild)
        return false;

    facts.GuildJoinCommitted = true;
    facts.CurrentlyGuilded = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ConfirmAfterGuildJoin) !=
        EnrollmentDecision::Accept)
        return false;
    facts.Current = StateAfterAcceptedTransition(EnrollmentRequest::ConfirmAfterGuildJoin);
    if (facts.Current != EnrollmentState::Persistent)
        return false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ConfirmAfterGuildJoin) !=
        EnrollmentDecision::AlreadyInRequestedState)
        return false;

    // An accidental guild loss, disband, transfer or restart is not retirement.
    facts.Current = StateAfterGuildChange(facts.Current, false);
    facts.Current = StateAfterGuildChange(facts.Current, true);
    if (facts.Current != EnrollmentState::Persistent)
        return false;

    facts.CurrentlyGuilded = false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::Accept)
        return false;
    facts.Current = StateAfterAcceptedTransition(EnrollmentRequest::ExplicitRetirement);
    if (facts.Current != EnrollmentState::Retired)
        return false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::AlreadyInRequestedState)
        return false;

    // Explicit re-recruitment of a retired bot starts a new durable stage.
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::Accept)
        return false;
    return true;
}

constexpr bool TestFailedRecruitment()
{
    EnrollmentFacts facts = ValidCandidate(EnrollmentState::Pending);
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::CancelFailedGuildJoin) !=
        EnrollmentDecision::Accept)
        return false;
    facts.Current = StateAfterAcceptedTransition(EnrollmentRequest::CancelFailedGuildJoin);
    if (facts.Current != EnrollmentState::Retired)
        return false;

    facts = ValidCandidate(EnrollmentState::Pending);
    facts.CurrentlyGuilded = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::CancelFailedGuildJoin) !=
        EnrollmentDecision::DenyStillGuilded)
        return false;
    facts.CurrentlyGuilded = false;
    facts.IsGrouped = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::CancelFailedGuildJoin) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;
    return true;
}

constexpr bool TestDeniedTransitions()
{
    EnrollmentFacts facts = ValidCandidate(EnrollmentState::Unknown);
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyUnknownState)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.AccountIsRandomBot = false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyNonRandomAccount)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.IsSelfbot = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyHumanControl)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.IsHumanControlled = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyHumanControl)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.IsAuthorized = false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyMissingAuthorization)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.HasRequestId = false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyMissingRequestId)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.HasActiveLease = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.IsGrouped = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    facts.HasActiveOperation = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::StageBeforeGuildJoin) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;

    facts = ValidCandidate(EnrollmentState::Absent);
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ConfirmAfterGuildJoin) !=
        EnrollmentDecision::DenyNoPendingEnrollment)
        return false;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyNeverEnrolled)
        return false;

    facts = ValidCandidate(EnrollmentState::Persistent);
    facts.CurrentlyGuilded = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyStillGuilded)
        return false;

    facts = ValidCandidate(EnrollmentState::Persistent);
    facts.HasActiveLease = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;
    facts.HasActiveLease = false;
    facts.HasActiveOperation = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;
    facts.HasActiveOperation = false;
    facts.IsGrouped = true;
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyActivityInProgress)
        return false;

    facts = ValidCandidate(EnrollmentState::Pending);
    if (DecideEnrollmentTransition(facts, EnrollmentRequest::ExplicitRetirement) !=
        EnrollmentDecision::DenyNoPendingEnrollment)
        return false;

    return true;
}

constexpr bool TestMaintenanceSnapshotPolicy()
{
    // Active mode, valid random bot: unknown, staged and enrolled are protected.
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Unknown, false, false))
        return false;
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Pending, false, false))
        return false;
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Persistent, false, false))
        return false;
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Absent, true, false))
        return false;
    if (!MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Retired, false, true))
        return false;

    // Confirmed filler and retired, non-guilded and unleased: legacy eligible.
    if (MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Absent, false, false))
        return false;
    if (MustProtectFromDestructiveMaintenance(true, true, false, false, EnrollmentState::Retired, false, false))
        return false;

    // Disabled mode or human-controlled candidate: autonomy adds no veto.
    if (MustProtectFromDestructiveMaintenance(false, true, false, false, EnrollmentState::Pending, true, true))
        return false;
    if (MustProtectFromDestructiveMaintenance(true, false, false, false, EnrollmentState::Persistent, true, true))
        return false;
    if (MustProtectFromDestructiveMaintenance(true, true, true, false, EnrollmentState::Persistent, true, true))
        return false;
    if (MustProtectFromDestructiveMaintenance(true, true, false, true, EnrollmentState::Persistent, true, true))
        return false;
    return true;
}
} // namespace

static_assert(TestTwoPhaseEnrollment(), "staged -> enrolled -> retired must be explicit");
static_assert(TestFailedRecruitment(), "incomplete enrollment must be safely cancelable");
static_assert(TestDeniedTransitions(), "unauthorized, active and unsafe writes must be rejected");
static_assert(TestMaintenanceSnapshotPolicy(), "unknown and pending must fail closed when active");

int main()
{
    assert(TestTwoPhaseEnrollment());
    assert(TestFailedRecruitment());
    assert(TestDeniedTransitions());
    assert(TestMaintenanceSnapshotPolicy());
}
