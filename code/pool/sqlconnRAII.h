#pragma once

#include "sqlconnpool.h"

#include <stdexcept>

class SqlConnRAII {
private:
    MYSQL* sql_ = nullptr;
    SqlConnPool* connpool_;

    static bool InitThread_() {
        // MySQL 的线程状态跟随线程，而不是跟随每一次连接借还。
        struct ThreadState {
            bool ready = mysql_thread_init() == 0;

            ~ThreadState() {
                if (ready) {
                    mysql_thread_end();
                }
            }
        };
        thread_local ThreadState state;
        return state.ready;
    }

public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connpool) : connpool_(connpool) {
        if (sql == nullptr || connpool_ == nullptr) {
            throw std::invalid_argument("SQL guard requires valid output and pool pointers");
        }
        *sql = nullptr;
        if (!InitThread_()) {
            return;
        }
        sql_ = connpool_->GetConn();
        *sql = sql_;
    }

    ~SqlConnRAII() {
        if (sql_ != nullptr) {
            connpool_->FreeConn(sql_);
        }
    }

    SqlConnRAII(const SqlConnRAII&) = delete;
    SqlConnRAII& operator=(const SqlConnRAII&) = delete;
    SqlConnRAII(SqlConnRAII&&) = delete;
    SqlConnRAII& operator=(SqlConnRAII&&) = delete;
};
