#include "OperationStore.h"

#include "MySQLPreparedStatement.h"
#include "OperationSql.h"

namespace PlayerbotAutonomy
{
void OperationConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(OpCount);
    PrepareStatement(OpInsert, OperationSql::Insert, CONNECTION_BOTH);
    PrepareStatement(OpChange, OperationSql::Change, CONNECTION_BOTH);
    PrepareStatement(OpJournal, OperationSql::Journal, CONNECTION_BOTH);
    PrepareStatement(OpMember, OperationSql::Member, CONNECTION_BOTH);
    PrepareStatement(OpLease, OperationSql::Lease, CONNECTION_BOTH);
    PrepareStatement(OpRelease, OperationSql::Release, CONNECTION_BOTH);
    PrepareStatement(OpLoad, OperationSql::Load, CONNECTION_BOTH);
    PrepareStatement(OpRecoverPage, OperationSql::RecoverPage, CONNECTION_BOTH);
}
bool OperationStore::Initialize(std::string const& info)
{
    if (info.empty())
        return false;
    SetConnectionInfo(info, 1, 1);
    return Open() == 0 && PrepareStatements();
}
PreparedStatement<OperationConnection>* OperationStore::Statement(OperationStatement index)
{
    return new PreparedStatement<OperationConnection>(index, GetPreparedStatementParamCount(index));
}
QueryCallback OperationStore::Load(uint64 id)
{
    auto* stmt = Statement(OpLoad);
    stmt->SetData(0, id);
    return AsyncQuery(stmt);
}
QueryCallback OperationStore::RecoverPage(uint64 cursor)
{
    auto* stmt = Statement(OpRecoverPage);
    stmt->SetData(0, cursor);
    return AsyncQuery(stmt);
}
TransactionCallback OperationStore::Save(Operation const& m, Operation const* before)
{
    auto transaction = std::make_shared<Transaction<OperationConnection>>();
    auto* stmt = Statement(before ? OpChange : OpInsert);
    if (!before)
    {
        stmt->SetData(0, m.Id);
        stmt->SetData(1, m.Revision);
        stmt->SetData(2, m.Guild);
        stmt->SetData(3, m.Leader);
        stmt->SetData(4, m.Faction);
        stmt->SetData(5, uint32(m.Type));
        stmt->SetData(6, std::string_view(OperationStateName(m.State)));
        stmt->SetData(7, m.CreatedAt);
        stmt->SetData(8, m.ChangedAt);
        stmt->SetData(9, m.Deadline);
        stmt->SetData(10, m.LeaseUntil);
        stmt->SetData(11, m.Attempts);
        stmt->SetData(12, m.Reason);
    }
    else
    {
        stmt->SetData(0, m.Revision);
        stmt->SetData(1, std::string_view(OperationStateName(m.State)));
        stmt->SetData(2, m.ChangedAt);
        stmt->SetData(3, m.Attempts);
        stmt->SetData(4, m.Reason);
        stmt->SetData(5, m.Id);
        stmt->SetData(6, before->Revision);
        stmt->SetData(7, std::string_view(OperationStateName(before->State)));
    }
    transaction->Append(stmt);
    auto* journal = Statement(OpJournal);
    journal->SetData(0, m.Id);
    journal->SetData(1, m.Revision);
    journal->SetData(2, std::string_view(OperationStateName(m.State)));
    journal->SetData(3, m.Reason);
    transaction->Append(journal);  // CHECK ROW_COUNT=1 also rejects zero-row CAS.
    for (uint32 i = 0; i < m.Members.size(); ++i)
    {
        auto const& member = m.Members[i];
        if (!before)
        {
            auto* s = Statement(OpMember);
            s->SetData(0, m.Id);
            s->SetData(1, i);
            s->SetData(2, member.Guid);
            s->SetData(3, member.Account);
            s->SetData(4, member.Guild);
            s->SetData(5, uint32(member.Role));
            transaction->Append(s);
        }
        if (m.State == OperationState::Reserved)
        {
            auto* s = Statement(OpLease);
            s->SetData(0, member.Guid);
            s->SetData(1, m.Id);
            s->SetData(2, m.LeaseUntil);
            s->SetData(3, member.Guid);
            s->SetData(4, member.Account);
            s->SetData(5, member.Guild);
            transaction->Append(s);
        }
    }
    if (Terminal(m.State))
    {
        auto* s = Statement(OpRelease);
        s->SetData(0, m.Id);
        transaction->Append(s);
    }
    return AsyncCommitTransaction(transaction);
}
bool OperationStore::Decode(PreparedQueryResult result, Operation& m, uint64& leases)
{
    if (!result || result->Fetch()[0].IsNull())
        return false;
    auto* f = result->Fetch();
    m = Operation{};
    m.Id = f[0].Get<uint64>();
    m.Revision = f[1].Get<uint64>();
    m.Guild = f[2].Get<uint32>();
    m.Leader = f[3].Get<uint32>();
    m.Faction = f[4].Get<uint32>();
    m.Type = OperationType(f[5].Get<uint32>());
    auto state = f[6].Get<std::string>();
    bool known = false;
    for (auto s :
         {OperationState::Proposed, OperationState::Validated, OperationState::Reserved, OperationState::Assembling,
          OperationState::Active, OperationState::Closing, OperationState::Completed, OperationState::Failed,
          OperationState::Cancelled, OperationState::Recovering})
        if (state == OperationStateName(s))
        {
            m.State = s;
            known = true;
        }
    if (!known || m.Type != OperationType::DungeonParty || !m.Id || !m.Guild || !m.Leader || m.Faction > 1)
        return false;
    m.CreatedAt = f[7].Get<uint64>();
    m.ChangedAt = f[8].Get<uint64>();
    m.Deadline = f[9].Get<uint64>();
    m.LeaseUntil = f[10].Get<uint64>();
    m.Attempts = f[11].Get<uint32>();
    m.Reason = f[12].Get<std::string>();
    // COUNT metadata is binary uint64 in the pinned core decoder.
    leases = f[18].Get<uint64>();
    do
    {
        f = result->Fetch();
        if (f[14].IsNull() || f[13].Get<uint32>() != m.Members.size())
            return false;
        RosterMember member;
        member.Guid = f[14].Get<uint32>();
        member.Account = f[15].Get<uint32>();
        member.Guild = f[16].Get<uint32>();
        member.Role = CombatRole(f[17].Get<uint32>());
        m.Members.push_back(member);
    } while (result->NextRow());
    return m.Members.size() == 5;
}
}  // namespace PlayerbotAutonomy
