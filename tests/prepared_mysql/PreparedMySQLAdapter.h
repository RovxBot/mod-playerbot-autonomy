#ifndef AUTONOMY_PREPARED_MYSQL_ADAPTER_H
#define AUTONOMY_PREPARED_MYSQL_ADAPTER_H

// Test transport only: production Store.cpp code supplies SQL and every value.
// The C API executes binary prepared statements on disposable MySQL. Result
// decoding uses the unmodified pinned core QueryResult.cpp and Field.cpp.
// Engine objects and async scheduling are simulated, not server acceptance.
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "CoreQueryResult.h"
#include "MySQLHacks.h"
inline bool FakeLostCommitAcknowledgement = false;

class QueryCallback
{
public:
    explicit QueryCallback(PreparedQueryResult result) : _result(std::move(result)) {}
    QueryCallback&& WithPreparedCallback(std::function<void(PreparedQueryResult)> callback)
    {
        _callback = std::move(callback);
        return std::move(*this);
    }
    bool InvokeIfReady()
    {
        _callback(_result);
        return true;
    }

private:
    PreparedQueryResult _result;
    std::function<void(PreparedQueryResult)> _callback;
};
class TransactionCallback
{
public:
    explicit TransactionCallback(bool result) : _result(result) {}
    void AfterComplete(std::function<void(bool)> callback) { _callback = std::move(callback); }
    bool InvokeIfReady()
    {
        _callback(_result);
        return true;
    }

private:
    bool _result;
    std::function<void(bool)> _callback;
};
struct TestStatement
{
    using Value = std::variant<std::monostate, uint32, uint64, std::string>;
    uint32 Index;
    std::vector<Value> Values;
    std::vector<unsigned> Assignments;
    TestStatement(uint32 index, uint8 count) : Index(index), Values(count), Assignments(count) {}
    virtual ~TestStatement() = default;
    void SetData(uint8 index, uint32 value)
    {
        Values.at(index) = value;
        ++Assignments.at(index);
    }
    void SetData(uint8 index, uint64 value)
    {
        Values.at(index) = value;
        ++Assignments.at(index);
    }
    void SetData(uint8 index, std::string_view value)
    {
        Values.at(index) = std::string(value);
        ++Assignments.at(index);
    }
};
template <class T>
class PreparedStatement : public TestStatement
{
public:
    using TestStatement::TestStatement;
};
struct TestTransaction
{
    std::vector<std::unique_ptr<TestStatement>> Statements;
    void Append(TestStatement* stmt) { Statements.emplace_back(stmt); }
};
template <class T>
class Transaction : public TestTransaction
{
};
struct MySQLConnectionInfo
{
};
class SQLOperation;
template <class T>
class ProducerConsumerQueue;
inline constexpr int CONNECTION_BOTH = 0;
class MySQLConnection
{
public:
    explicit MySQLConnection(MySQLConnectionInfo&) {}
    MySQLConnection(ProducerConsumerQueue<SQLOperation*>*, MySQLConnectionInfo&) {}
    virtual ~MySQLConnection() = default;
    virtual void DoPrepareStatements() = 0;
    void PrepareStatement(uint32 index, char const* sql, int) { m_stmts.at(index) = sql; }
    bool m_reconnecting = false;
    std::vector<std::string> m_stmts;
};

class ModuleDatabasePool
{
public:
    virtual ~ModuleDatabasePool() { Close(); }
    void SetConnectionInfo(std::string const&, int, int) {}
    uint32 Open()
    {
        MySQLConnectionInfo info;
        _connection.reset(CreateConnection(info));
        _mysql = mysql_init(nullptr);
        auto const* database = std::getenv("AUTONOMY_MYSQL_DATABASE");
        std::string const name = database ? database : "";
        bool const suffix = name.size() > 14 && name.substr(0, 14) == "autonomy_test_" &&
                            std::all_of(name.begin() + 14, name.end(),
                                        [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
        if (name != "autonomy_test" && !suffix)
            throw std::runtime_error("disposable autonomy_test database required");
        if (!mysql_real_connect(_mysql, std::getenv("AUTONOMY_MYSQL_HOST"),
                                std::getenv("AUTONOMY_MYSQL_USER") ? std::getenv("AUTONOMY_MYSQL_USER") : "root",
                                nullptr, database,
                                std::getenv("AUTONOMY_MYSQL_PORT") ? std::atoi(std::getenv("AUTONOMY_MYSQL_PORT")) : 0,
                                std::getenv("AUTONOMY_MYSQL_SOCKET"), 0))
            throw std::runtime_error(mysql_error(_mysql));
        if (mysql_get_server_version(_mysql) < 80000 || mysql_get_server_version(_mysql) >= 90000)
            throw std::runtime_error("MySQL 8 required");
        return 0;
    }
    bool PrepareStatements()
    {
        _connection->DoPrepareStatements();
        for (auto const& sql : _connection->m_stmts)
        {
            auto* stmt = mysql_stmt_init(_mysql);
            if (mysql_stmt_prepare(stmt, sql.data(), sql.size()))
                throw std::runtime_error(mysql_stmt_error(stmt));
            bool updateLengths = true;
            mysql_stmt_attr_set(stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &updateLengths);
            _statements.push_back(stmt);
        }
        return true;
    }
    void Close()
    {
        for (auto* stmt : _statements)
            mysql_stmt_close(stmt);
        _statements.clear();
        if (_mysql)
            mysql_close(_mysql);
        _mysql = nullptr;
    }
    uint8 GetPreparedStatementParamCount(uint32 index) const { return mysql_stmt_param_count(_statements.at(index)); }
    PreparedQueryResult Read(TestStatement const& input)
    {
        auto* stmt = Execute(input);
        if (!stmt)
            throw std::runtime_error("prepared query failed");
        auto* meta = mysql_stmt_result_metadata(stmt);
        assert(meta);
        return std::make_shared<PreparedResultSet>(reinterpret_cast<MySQLStmt*>(stmt),
                                                   reinterpret_cast<MySQLResult*>(meta), 0, mysql_num_fields(meta));
    }
    QueryCallback AsyncQuery(TestStatement* input)
    {
        std::unique_ptr<TestStatement> owned(input);
        return QueryCallback(Read(*input));
    }
    TransactionCallback AsyncCommitTransaction(std::shared_ptr<TestTransaction> transaction)
    {
        Raw("START TRANSACTION");
        for (auto const& stmt : transaction->Statements)
        {
            if (!Execute(*stmt))
            {
                Raw("ROLLBACK");
                return TransactionCallback(false);
            }
        }
        Raw("COMMIT");
        return TransactionCallback(!FakeLostCommitAcknowledgement);
    }
    void Raw(std::string const& sql)
    {
        if (mysql_real_query(_mysql, sql.data(), sql.size()))
            throw std::runtime_error(mysql_error(_mysql));
        if (auto* result = mysql_store_result(_mysql))
            mysql_free_result(result);
    }
    PreparedQueryResult Inspect(std::string const& sql)
    {
        auto* stmt = mysql_stmt_init(_mysql);
        _statements.push_back(stmt);
        if (mysql_stmt_prepare(stmt, sql.data(), sql.size()) || mysql_stmt_execute(stmt))
            throw std::runtime_error(mysql_stmt_error(stmt));
        bool updateLengths = true;
        mysql_stmt_attr_set(stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &updateLengths);
        auto* meta = mysql_stmt_result_metadata(stmt);
        assert(meta);
        return std::make_shared<PreparedResultSet>(reinterpret_cast<MySQLStmt*>(stmt),
                                                   reinterpret_cast<MySQLResult*>(meta), 0, mysql_num_fields(meta));
    }
    TestStatement LastChange{0, 0};
    std::vector<TestStatement::Value> LastValues;
    std::string LastFailure;

protected:
    virtual MySQLConnection* CreateConnection(MySQLConnectionInfo&) = 0;
    virtual MySQLConnection* CreateAsyncConnection(ProducerConsumerQueue<SQLOperation*>*, MySQLConnectionInfo&) = 0;

private:
    MYSQL_STMT* Execute(TestStatement const& input)
    {
        auto* stmt = _statements.at(input.Index);
        if (input.Values.size() == 9)
            LastChange = input;
        std::vector<MYSQL_BIND> binds(input.Values.size());
        for (std::size_t i = 0; i < binds.size(); ++i)
        {
            assert(input.Assignments[i] == 1);  // each production placeholder exactly once
            std::visit(
                [&](auto const& value)
                {
                    using T = std::decay_t<decltype(value)>;
                    auto& bind = binds[i];
                    if constexpr (std::is_same_v<T, std::monostate>)
                        throw std::runtime_error("unbound production parameter");
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        bind.buffer_type = MYSQL_TYPE_STRING;
                        bind.buffer = const_cast<char*>(value.data());
                        bind.buffer_length = value.size();
                    }
                    else
                    {
                        bind.buffer_type = sizeof(T) == 8 ? MYSQL_TYPE_LONGLONG : MYSQL_TYPE_LONG;
                        bind.is_unsigned = true;
                        bind.buffer = const_cast<T*>(&value);
                    }
                },
                input.Values[i]);
        }
        if (mysql_stmt_bind_param(stmt, binds.data()) || mysql_stmt_execute(stmt))
        {
            LastFailure = mysql_stmt_error(stmt);
            return nullptr;
        }
        LastValues = input.Values;
        return stmt;
    }
    MYSQL* _mysql = nullptr;
    std::unique_ptr<MySQLConnection> _connection;
    std::vector<MYSQL_STMT*> _statements;
};
#endif
