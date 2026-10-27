#include "userservice.h"

#include <cstring>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"

using namespace std;

namespace {
struct StatementDeleter {
    void operator()(MYSQL_STMT* statement) const {
        mysql_stmt_close(statement);
    }
};
using Statement = unique_ptr<MYSQL_STMT, StatementDeleter>;

Statement Execute(MYSQL* connection, const char* query, initializer_list<string_view> parameters) {
    Statement statement(mysql_stmt_init(connection));
    if (!statement) {
        LOG_ERROR("Cannot create database statement");
        return nullptr;
    }
    if (mysql_stmt_prepare(statement.get(), query, static_cast<unsigned long>(strlen(query))) != 0) {
        LOG_ERROR("Cannot prepare database statement (%u)", mysql_stmt_errno(statement.get()));
        return nullptr;
    }

    //参数与SQL分开传递,避免引号改变语句含义或固定缓冲区截断
    vector<MYSQL_BIND> bindings(parameters.size());
    vector<unsigned long> lengths(parameters.size());
    size_t index = 0;
    for (const auto parameter : parameters) {
        lengths[index] = static_cast<unsigned long>(parameter.size());
        bindings[index].buffer_type = MYSQL_TYPE_STRING;
        bindings[index].buffer = const_cast<char*>(parameter.data());
        bindings[index].buffer_length = lengths[index];
        bindings[index].length = &lengths[index];
        ++index;
    }
    if (mysql_stmt_bind_param(statement.get(), bindings.data()) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        //只记录错误码,不把密码或包含参数的数据写入日志
        LOG_ERROR("Cannot execute database statement (%u)", mysql_stmt_errno(statement.get()));
        return nullptr;
    }
    return statement;
}
}

bool UserService::Verify(const string& name, const string& password, bool isLogin) {
    if (name.empty() || password.empty()) {
        return false;
    }
    MYSQL* connection = nullptr;
    SqlConnRAII guard(&connection, SqlConnPool::Instance());
    if (connection == nullptr) {
        LOG_ERROR("No database connection available");
        return false;
    }

    int marker = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &marker;
    result.buffer_length = sizeof(marker);
    Statement lookup = isLogin
        ? Execute(connection, "SELECT 1 FROM user WHERE username = ? AND "
                  "CAST(password AS BINARY) = CAST(? AS BINARY) LIMIT 1", {name, password})
        : Execute(connection, "SELECT 1 FROM user WHERE username = ? LIMIT 1", {name});
    if (!lookup || mysql_stmt_bind_result(lookup.get(), &result) != 0 ||
        mysql_stmt_store_result(lookup.get()) != 0) {
        LOG_ERROR("Cannot read user lookup result");
        return false;
    }
    const bool exists = mysql_stmt_num_rows(lookup.get()) != 0;
    if (isLogin) {
        return exists;
    }
    if (exists) {
        return false;
    }
    Statement insertion = Execute(connection, "INSERT INTO user(username,password) VALUES(?,?)", {name, password});
    return insertion && mysql_stmt_affected_rows(insertion.get()) == 1;
}
