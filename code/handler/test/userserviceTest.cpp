#include "../userservice.h"
#include "../../pool/sqlconnpool.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

using namespace std;

namespace {
string scenario;
unordered_set<MYSQL*> connections;
int insertions = 0;
int statements = 0;
int threadStarts = 0;
vector<string> suppliedParameters;

struct FakeStatement {
    string query;
    vector<MYSQL_BIND> parameters;
    uint64_t rows = 0;
    bool resultBound = false;
};

void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

FakeStatement* Fake(MYSQL_STMT* statement) {
    return reinterpret_cast<FakeStatement*>(statement);
}
}

extern "C" int __wrap_mysql_server_init(int, char**, char**) { return 0; }
extern "C" MYSQL* __wrap_mysql_init(MYSQL*) {
    auto* connection = new MYSQL{};
    connections.insert(connection);
    return connection;
}
extern "C" MYSQL* __wrap_mysql_real_connect(MYSQL* connection, const char*, const char*, const char*,
                                           const char*, unsigned int, const char*, unsigned long) {
    return connection;
}
extern "C" void __wrap_mysql_close(MYSQL* connection) {
    if (connections.erase(connection) != 1 || statements != 0) {
        abort();
    }
    delete connection;
}
extern "C" auto __wrap_mysql_thread_init() -> decltype(mysql_thread_init()) {
    ++threadStarts;
    return 0;
}
extern "C" void __wrap_mysql_thread_end() {}
extern "C" MYSQL_STMT* __wrap_mysql_stmt_init(MYSQL*) {
    if (scenario == "statement_init_failure") {
        return nullptr;
    }
    ++statements;
    return reinterpret_cast<MYSQL_STMT*>(new FakeStatement{});
}
extern "C" int __wrap_mysql_stmt_prepare(MYSQL_STMT* statement, const char* query, unsigned long length) {
    Fake(statement)->query.assign(query, length);
    return scenario == "prepare_failure" ? 1 : 0;
}
extern "C" auto __wrap_mysql_stmt_bind_param(MYSQL_STMT* statement, MYSQL_BIND* bindings)
    -> decltype(mysql_stmt_bind_param(statement, bindings)) {
    if (scenario == "bind_failure") {
        return true;
    }
    const auto count = count_if(Fake(statement)->query.begin(), Fake(statement)->query.end(), [](char c) {
        return c == '?';
    });
    Fake(statement)->parameters.assign(bindings, bindings + count);
    return false;
}
extern "C" int __wrap_mysql_stmt_execute(MYSQL_STMT* statement) {
    auto* fake = Fake(statement);
    vector<string> values;
    for (const auto& parameter : fake->parameters) {
        Require(parameter.buffer_type == MYSQL_TYPE_STRING && parameter.length != nullptr,
                "statement input was not bound as an explicit-length string");
        values.emplace_back(static_cast<const char*>(parameter.buffer), *parameter.length);
        Require(values.back().empty() || fake->query.find(values.back()) == string::npos,
                "input was interpolated into SQL");
    }
    suppliedParameters = values;
    if (fake->query.starts_with("INSERT")) {
        ++insertions;
        return scenario == "insert_failure" ? 1 : 0;
    }
    fake->rows = scenario == "login_success" || scenario == "special_parameters" ||
                 scenario == "registration_exists" ? 1 : 0;
    return scenario == "query_failure" ? 1 : 0;
}
extern "C" auto __wrap_mysql_stmt_bind_result(MYSQL_STMT* statement, MYSQL_BIND* result)
    -> decltype(mysql_stmt_bind_result(statement, result)) {
    Require(result->buffer_type == MYSQL_TYPE_LONG, "lookup result was not bound");
    Fake(statement)->resultBound = true;
    return scenario == "result_bind_failure";
}
extern "C" int __wrap_mysql_stmt_store_result(MYSQL_STMT* statement) {
    Require(Fake(statement)->resultBound, "result storage preceded result binding");
    return scenario == "store_failure" ? 1 : 0;
}
extern "C" auto __wrap_mysql_stmt_num_rows(MYSQL_STMT* statement) -> decltype(mysql_stmt_num_rows(statement)) {
    return Fake(statement)->rows;
}
extern "C" auto __wrap_mysql_stmt_affected_rows(MYSQL_STMT* statement)
    -> decltype(mysql_stmt_affected_rows(statement)) {
    return scenario == "zero_insert_affected" ? 0 : 1;
}
extern "C" unsigned int __wrap_mysql_stmt_errno(MYSQL_STMT*) { return 1064; }
extern "C" auto __wrap_mysql_stmt_close(MYSQL_STMT* statement) -> decltype(mysql_stmt_close(statement)) {
    --statements;
    delete Fake(statement);
    return false;
}

int main(int argc, char* argv[]) {
    const vector<string> cases = {"empty_credentials", "login_success", "login_failure", "registration_success",
        "registration_exists", "insert_failure", "query_failure", "statement_init_failure", "prepare_failure",
        "bind_failure", "result_bind_failure", "store_failure", "zero_insert_affected", "special_parameters"};
    if (argc != 2) {
        return 2;
    }
    scenario = argv[1];
    if (scenario == "--list") {
        for (const auto& item : cases) {
            cout << item << '\n';
        }
        return 0;
    }
    try {
        Require(find(cases.begin(), cases.end(), scenario) != cases.end(), "unknown test case");
        auto* pool = SqlConnPool::Instance();
        pool->Init("localhost", 3306, "test-user", "test-password", "testdb", 1);
        const string name = scenario == "special_parameters" ? "alice' OR 1=1 --" : "alice";
        const string password = scenario == "special_parameters" ? string("a'\0b", 4) : "test-secret";
        const bool registration = scenario.starts_with("registration") || scenario == "insert_failure" ||
                                  scenario == "zero_insert_affected";
        const bool expected = scenario == "login_success" || scenario == "registration_success" ||
                              scenario == "special_parameters";
        const bool result = UserService::Verify(scenario == "empty_credentials" ? "" : name,
                                                password, !registration);
        Require(result == expected, "wrong login or registration outcome");
        Require(statements == 0 && pool->GetFreeConnCnt() == 1, "statement or borrowed connection leaked");
        Require(insertions == (scenario == "registration_success" || scenario == "insert_failure" ||
                                scenario == "zero_insert_affected" ? 1 : 0), "registration inserted after a failure or duplicate");
        if (scenario == "special_parameters") {
            Require(suppliedParameters == vector<string>{name, password}, "bound parameters lost quotes or NUL bytes");
        }
        if (scenario == "empty_credentials") {
            Require(threadStarts == 0, "empty credentials accessed the database");
        }
        pool->ClosePool();
        Require(connections.empty(), "pool connection leaked");
        cout << "PASS " << scenario << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "FAIL " << scenario << ": " << error.what() << '\n';
        return 1;
    }
}
