#include "sqlconnpool.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

SqlConnPool::SqlConnPool() {
    // 在创建工作线程前，由主线程首次取得单例并初始化 MySQL 库
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        throw runtime_error("MySQL library initialization failed");
    }
}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}

SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool pool;
    return &pool;
}

void SqlConnPool::Init(const char* host, int port, const char* user,
                       const char* pwd, const char* dbName, int connSize) {
    if (connSize <= 0) {
        throw invalid_argument("database.poolSize must be positive");
    }
    if (port < 1 || port > 65535) {
        throw invalid_argument("database.port must be between 1 and 65535");
    }

    lock_guard<mutex> locker(mtx_);
    if (initialized_ || closed_) {
        throw logic_error("SQL connection pool cannot be initialized again");
    }

    //初始化创建完全部的连接
    using Connection = unique_ptr<MYSQL, decltype(&mysql_close)>;
    vector<Connection> pending;
    pending.reserve(static_cast<size_t>(connSize));
    for (int i = 0; i < connSize; ++i) {
        Connection sql(mysql_init(nullptr), &mysql_close);
        if (!sql) {
            throw runtime_error("mysql_init failed");
        }
        if (mysql_real_connect(sql.get(), host, user, pwd, dbName,
                               static_cast<unsigned int>(port), nullptr, 0) == nullptr) {
            throw runtime_error("MySQL connect failed (" +
                                to_string(mysql_errno(sql.get())) + "): " +
                                mysql_error(sql.get()));
        }
        pending.push_back(move(sql));
    }

    // 所有连接成功后才发布。构造队列失败时，pending 仍负责释放全部连接。
    queue<MYSQL*> ready;
    for (const auto& sql : pending) {
        ready.push(sql.get());
    }
    connQue_.swap(ready);
    for (auto& sql : pending) {
        sql.release();
    }
    initialized_ = true;
}

MYSQL* SqlConnPool::GetConn() {
    // 检查与取出必须在同一个临界区；返回后释放锁，调用者再执行 SQL。
    lock_guard<mutex> locker(mtx_);
    if (!initialized_ || closed_ || connQue_.empty()) {
        return nullptr;
    }
    MYSQL* sql = connQue_.front();
    connQue_.pop();
    return sql;
}

void SqlConnPool::FreeConn(MYSQL* conn) {
    if (conn == nullptr) {
        return;
    }
    unique_lock<mutex> locker(mtx_);
    if (closed_) {
        locker.unlock();
        mysql_close(conn);
        return;
    }
    connQue_.push(conn);
}

int SqlConnPool::GetFreeConnCnt() {
    lock_guard<mutex> locker(mtx_);
    return static_cast<int>(connQue_.size());
}

void SqlConnPool::ClosePool() {
    queue<MYSQL*> idle;
    {
        lock_guard<mutex> locker(mtx_);
        closed_ = true;
        idle.swap(connQue_);
    }
    while (!idle.empty()) {
        mysql_close(idle.front());
        idle.pop();
    }
    // mysql_library_end() 是进程级清理，需等全部 MySQL 使用线程退出后执行。
    // 不能在这里清理仍有工作线程使用的客户端库。
}
