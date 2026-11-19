#ifndef PLAYERBOT_AUTONOMY_OPERATION_STORE_H
#define PLAYERBOT_AUTONOMY_OPERATION_STORE_H
#include "OperationCoordinator.h"
#include "Persistence/EnrollmentStore.h"
namespace PlayerbotAutonomy
{
enum OperationStatement : uint32
{
    OpInsert,
    OpChange,
    OpJournal,
    OpMember,
    OpLease,
    OpRelease,
    OpLoad,
    OpRecoverPage,
    OpExecution,
    OpGroupSnapshot,
    OpGroupAbsent,
    OpCount
};
class OperationConnection final : public MySQLConnection
{
public:
    using Statements = OperationStatement;
    explicit OperationConnection(MySQLConnectionInfo& info) : MySQLConnection(info) {}
    OperationConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& info)
        : MySQLConnection(queue, info)
    {
    }
    void DoPrepareStatements() override;
};
class OperationStore final : public ModuleDatabasePool
{
public:
    bool Initialize(std::string const& info);
    QueryCallback Load(uint64 id);
    QueryCallback RecoverPage(uint64 cursor);
    QueryCallback GroupSnapshot(Operation const& operation);
    QueryCallback GroupAbsent(Operation const& operation);
    TransactionCallback Save(Operation const& next, Operation const* before);
    static bool Decode(PreparedQueryResult result, Operation& out, uint64& leases);

protected:
    MySQLConnection* CreateConnection(MySQLConnectionInfo& info) override { return new OperationConnection(info); }
    MySQLConnection* CreateAsyncConnection(ProducerConsumerQueue<SQLOperation*>* queue,
                                           MySQLConnectionInfo& info) override
    {
        return new OperationConnection(queue, info);
    }

private:
    PreparedStatement<OperationConnection>* Statement(OperationStatement index);
};
}  // namespace PlayerbotAutonomy
#endif
