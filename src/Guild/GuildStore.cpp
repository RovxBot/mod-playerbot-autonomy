#include "GuildStore.h"

#include "GuildSql.h"
#include "MySQLPreparedStatement.h"
namespace PlayerbotAutonomy
{
void GuildConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(GuildStatementCount);
    PrepareStatement(GuildLoad, GuildSql::Load, CONNECTION_BOTH);
    PrepareStatement(GuildAuditPage, GuildSql::AuditPage, CONNECTION_BOTH);
    PrepareStatement(GuildInsert, GuildSql::Insert, CONNECTION_BOTH);
    PrepareStatement(GuildChange, GuildSql::Change, CONNECTION_BOTH);
    PrepareStatement(GuildAudit, GuildSql::Audit, CONNECTION_BOTH);
}
bool GuildStore::Initialize(std::string const& info)
{
    if (info.empty())
        return false;
    SetConnectionInfo(info, 1, 1);
    return Open() == 0 && PrepareStatements();
}
PreparedStatement<GuildConnection>* GuildStore::Statement(GuildStatement index)
{
    return new PreparedStatement<GuildConnection>(index, GetPreparedStatementParamCount(index));
}
QueryCallback GuildStore::Load() { return AsyncQuery(Statement(GuildLoad)); }
QueryCallback GuildStore::AuditPage(uint32 cursor)
{
    auto* stmt = Statement(GuildAuditPage);
    stmt->SetData(0, cursor);
    return AsyncQuery(stmt);
}
TransactionCallback GuildStore::Save(GuildIntent const& m, std::optional<GuildPhase> before)
{
    auto transaction = std::make_shared<Transaction<GuildConnection>>();
    auto* stmt = Statement(before ? GuildChange : GuildInsert);
    if (!before)
    {
        stmt->SetData(0, m.Slot);
        stmt->SetData(1, m.Faction);
        stmt->SetData(2, m.Name);
        stmt->SetData(3, m.Reason);
        stmt->SetData(4, m.StageRevision);
        stmt->SetData(5, m.StageRequest);
        stmt->SetData(6, m.Founder);
        stmt->SetData(7, m.Account);
    }
    else
    {
        stmt->SetData(0, std::string_view(PhaseName(m.Phase)));
        stmt->SetData(1, m.Guild);
        stmt->SetData(2, m.Reason);
        stmt->SetData(3, m.Slot);
        stmt->SetData(4, m.StageRevision);
        stmt->SetData(5, m.StageRequest);
        stmt->SetData(6, m.Founder);
        stmt->SetData(7, m.Account);
        stmt->SetData(6, std::string_view(PhaseName(*before)));
        stmt->SetData(7, std::string_view(PhaseName(m.Phase)));
        stmt->SetData(8, m.Guild);
    }
    transaction->Append(stmt);
    auto* audit = Statement(GuildAudit);
    audit->SetData(0, m.Slot);
    audit->SetData(1, std::string_view(PhaseName(m.Phase)));
    audit->SetData(2, m.Guild);
    audit->SetData(3, m.Reason);
    transaction->Append(audit);
    return AsyncCommitTransaction(transaction);
}
}  // namespace PlayerbotAutonomy
