#ifndef PLAYERBOT_AUTONOMY_ENROLLMENT_STORE_H
#define PLAYERBOT_AUTONOMY_ENROLLMENT_STORE_H

#include <string>

#include "EnrollmentCache.h"
#include "EnrollmentTransition.h"
#include "ModuleDatabasePool.h"
#include "MySQLConnection.h"
#include "PreparedStatement.h"
#include "QueryCallback.h"
#include "Transaction.h"

namespace PlayerbotAutonomy
{
enum EnrollmentStatement : uint32
{
    SelectSchema,
    SelectPage,
    SelectReceipt,
    InsertStage,
    UpdateTransition,
    InsertTransitionReceipt,
    InsertAudit,
    InsertOrphan,
    StatementCount
};

class EnrollmentConnection final : public MySQLConnection
{
public:
    using Statements = EnrollmentStatement;
    explicit EnrollmentConnection(MySQLConnectionInfo& info) : MySQLConnection(info) {}
    EnrollmentConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& info)
        : MySQLConnection(queue, info)
    {
    }
    void DoPrepareStatements() override;
};

// Same characters database, independent prepared-statement namespace. Exactly
// one async worker; synchronous connections are used only for open/prepare.
class EnrollmentStore final : public ModuleDatabasePool
{
public:
    bool Initialize(std::string const& characterDatabaseInfo);
    QueryCallback CheckSchema();
    QueryCallback LoadPage(uint32 cursor, uint32 limit);
    QueryCallback FindReceipt(std::string const& requestId);
    TransactionCallback Commit(TransitionRequest const& request, EnrollmentRecord const& before);
    TransactionCallback ReconcileOrphan(uint32 guid);

protected:
    MySQLConnection* CreateConnection(MySQLConnectionInfo& info) override { return new EnrollmentConnection(info); }
    MySQLConnection* CreateAsyncConnection(ProducerConsumerQueue<SQLOperation*>* queue,
                                           MySQLConnectionInfo& info) override
    {
        return new EnrollmentConnection(queue, info);
    }

private:
    PreparedStatement<EnrollmentConnection>* Statement(EnrollmentStatement index);
};
}  // namespace PlayerbotAutonomy
#endif
