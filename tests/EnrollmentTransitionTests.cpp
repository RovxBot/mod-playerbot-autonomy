#include <cassert>

#include "Persistence/EnrollmentTransition.h"

using namespace PlayerbotAutonomy;

int main()
{
    assert(ValidRequestId("00000000-0000-0000-0000-00000000abcd"));
    assert(!ValidRequestId("00000000-0000-0000-0000-00000000ABCD"));
    assert(!ValidRequestId("not-a-uuid"));
    TransitionRequest request{42, 7, 3, 9, EnrollmentRequest::ConfirmAfterGuildJoin, "request-id", "adopt"};
    TransitionReceipt receipt{42, 7, 3, 9, 10, "CONFIRM", EnrollmentState::Persistent, "AUTHORIZED_ADMIN", 0, "adopt"};
    assert(MatchesReceipt(request, receipt));
    auto changed = receipt;
    changed.Guid++;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.Account++;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.Guild++;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.ExpectedRevision++;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.ResultingRevision++;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.Event = "RETIRE";
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.Outcome = EnrollmentState::Retired;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.ActorKind = "RECRUITMENT";
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.ActorGuid = 1;
    assert(!MatchesReceipt(request, changed));
    changed = receipt;
    changed.Reason = "different reason";
    assert(!MatchesReceipt(request, changed));
}
