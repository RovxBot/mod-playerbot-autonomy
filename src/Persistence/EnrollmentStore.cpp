#include "EnrollmentStore.h"

#include "EnrollmentSql.h"
#include "MySQLPreparedStatement.h"

namespace PlayerbotAutonomy
{
void EnrollmentConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(StatementCount);
    PrepareStatement(SelectSchema, Sql::Schema, CONNECTION_BOTH);
    PrepareStatement(SelectPage, Sql::Page, CONNECTION_BOTH);
    PrepareStatement(SelectReceipt, Sql::Receipt, CONNECTION_BOTH);
    PrepareStatement(InsertStage, Sql::Stage, CONNECTION_BOTH);
    PrepareStatement(UpdateTransition, Sql::Transition, CONNECTION_BOTH);
    PrepareStatement(InsertTransitionReceipt, Sql::InsertReceipt, CONNECTION_BOTH);
    PrepareStatement(InsertAudit, Sql::Audit, CONNECTION_BOTH);
    PrepareStatement(InsertOrphan, Sql::Orphan, CONNECTION_BOTH);
}

bool EnrollmentStore::Initialize(std::string const& info)
{
    if (info.empty())
        return false;
    SetConnectionInfo(info, 1, 1);
    return Open() == 0 && PrepareStatements();
}

PreparedStatement<EnrollmentConnection>* EnrollmentStore::Statement(EnrollmentStatement index)
{
    return new PreparedStatement<EnrollmentConnection>(index, GetPreparedStatementParamCount(index));
}

QueryCallback EnrollmentStore::CheckSchema() { return AsyncQuery(Statement(SelectSchema)); }

QueryCallback EnrollmentStore::LoadPage(uint32 cursor, uint32 limit)
{
    auto* stmt = Statement(SelectPage);
    stmt->SetData(0, cursor);
    stmt->SetData(1, limit);
    stmt->SetData(2, cursor);
    stmt->SetData(3, limit);
    stmt->SetData(4, limit);
    return AsyncQuery(stmt);
}

QueryCallback EnrollmentStore::FindReceipt(std::string const& requestId)
{
    auto* stmt = Statement(SelectReceipt);
    stmt->SetData(0, requestId);
    return AsyncQuery(stmt);
}

TransactionCallback EnrollmentStore::Commit(TransitionRequest const& request, EnrollmentRecord const& before)
{
    auto transaction = std::make_shared<Transaction<EnrollmentConnection>>();
    char const* event = EventName(request.Action);
    char const* after = StateName(StateAfterAcceptedTransition(request.Action));
    if (before.State == EnrollmentState::Absent)
    {
        auto* stmt = Statement(InsertStage);
        stmt->SetData(0, request.Guild);
        stmt->SetData(1, request.Guid);
        stmt->SetData(2, request.Account);
        transaction->Append(stmt);
    }
    else
    {
        auto* stmt = Statement(UpdateTransition);
        stmt->SetData(0, std::string_view(after));
        stmt->SetData(1, std::string_view(event));
        stmt->SetData(2, request.Guild);
        stmt->SetData(3, std::string_view(event));
        stmt->SetData(4, std::string_view(event));
        stmt->SetData(5, request.Guid);
        stmt->SetData(6, request.Account);
        stmt->SetData(7, request.ExpectedRevision);
        stmt->SetData(8, std::string_view(StateName(before.State)));
        stmt->SetData(9, std::string_view(event));
        stmt->SetData(10, request.Guild);
        stmt->SetData(11, std::string_view(event));
        stmt->SetData(12, std::string_view(event));
        transaction->Append(stmt);
    }
    auto* receipt = Statement(InsertTransitionReceipt);
    receipt->SetData(0, request.RequestId);
    receipt->SetData(1, request.ExpectedRevision);
    receipt->SetData(2, request.ExpectedRevision + 1);
    transaction->Append(receipt);

    auto* audit = Statement(InsertAudit);
    audit->SetData(0, request.RequestId);
    audit->SetData(1, request.Guid);
    audit->SetData(2, request.Account);
    audit->SetData(3, std::string_view(event));
    audit->SetData(4, std::string_view(StateName(before.State)));
    audit->SetData(5, std::string_view(after));
    audit->SetData(6, request.Guild);
    audit->SetData(7, std::string_view(request.Director ? "RECRUITMENT" : "AUTHORIZED_ADMIN"));
    audit->SetData(8, request.ActorGuid);
    audit->SetData(9, request.Reason);
    transaction->Append(audit);
    return AsyncCommitTransaction(transaction);
}

TransactionCallback EnrollmentStore::ReconcileOrphan(uint32 guid)
{
    // Reserved non-UUID namespace: clients cannot supply reconciliation keys.
    std::string const id = "orphan-v1-" + std::to_string(guid);
    auto* stmt = Statement(InsertOrphan);
    stmt->SetData(0, id);
    stmt->SetData(1, guid);
    stmt->SetData(2, id);
    auto transaction = std::make_shared<Transaction<EnrollmentConnection>>();
    transaction->Append(stmt);
    return AsyncCommitTransaction(transaction);
}
}  // namespace PlayerbotAutonomy
