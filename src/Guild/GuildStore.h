#ifndef PLAYERBOT_AUTONOMY_GUILD_STORE_H
#define PLAYERBOT_AUTONOMY_GUILD_STORE_H
#include "GuildDirector.h"
#include "Persistence/EnrollmentStore.h"
namespace PlayerbotAutonomy
{
enum GuildStatement : uint32
{
    GuildLoad,
    GuildAuditPage,
    GuildInsert,
    GuildChange,
    GuildAudit,
    GuildStatementCount
};
class GuildConnection final : public MySQLConnection
{
public:
    using Statements = GuildStatement;
    explicit GuildConnection(MySQLConnectionInfo& info) : MySQLConnection(info) {}
    GuildConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& info)
        : MySQLConnection(queue, info)
    {
    }
    void DoPrepareStatements() override;
};
class GuildStore final : public ModuleDatabasePool
{
public:
    bool Initialize(std::string const& info);
    QueryCallback Load();
    QueryCallback AuditPage(uint32 cursor);
    TransactionCallback Save(GuildIntent const& intent, std::optional<GuildPhase> before);

protected:
    MySQLConnection* CreateConnection(MySQLConnectionInfo& info) override { return new GuildConnection(info); }
    MySQLConnection* CreateAsyncConnection(ProducerConsumerQueue<SQLOperation*>* queue,
                                           MySQLConnectionInfo& info) override
    {
        return new GuildConnection(queue, info);
    }

private:
    PreparedStatement<GuildConnection>* Statement(GuildStatement index);
};
}  // namespace PlayerbotAutonomy
#endif
