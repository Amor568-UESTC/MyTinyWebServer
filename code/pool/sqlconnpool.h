#pragma once

#include<mysql/mysql.h>
#include<string>
#include<queue>
#include<mutex>
#include<semaphore.h>
#include<thread>

#include"../log/log.h"

class SqlConnPool
{
private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;
    int useCnt_;
    int freeCnt_;

    std::queue<MYSQL*> connQue_;
    std::mutex mtx_;
    sem_t semId_;

public:
    static SqlConnPool* Instance();
    MYSQL* GetConn();
    void FreeConn(MYSQL* conn);
    int GetFreeConnCnt();

    void Init(const char* host,int port,const char* user,
              const char* pwd,const char* dbName,int connSize);
    void ClosePool();
};