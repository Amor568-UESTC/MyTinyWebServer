#pragma once

#include <mysql/mysql.h>
#include <mutex>
#include <queue>

class SqlConnPool {
private:
    SqlConnPool();
    ~SqlConnPool();

    bool initialized_ = false;
    bool closed_ = false;
    std::queue<MYSQL*> connQue_;
    std::mutex mtx_;

public:
    SqlConnPool(const SqlConnPool&) = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;

    static SqlConnPool* Instance();
    // 取得互斥锁后，池未就绪、已关闭或为空时返回 nullptr，不等待连接归还。
    MYSQL* GetConn();
    // 只归还从本池借出的连接，且每次借出只能归还一次。
    void FreeConn(MYSQL* conn);
    int GetFreeConnCnt();
    void Init(const char* host, int port, const char* user,
              const char* pwd, const char* dbName, int connSize);
    // 关闭空闲连接；借出的连接归还时再关闭。销毁前必须结束所有使用者
    void ClosePool();
};
