#include"sqlconnpool.h"

using namespace std;

SqlConnPool::SqlConnPool()
{
    useCnt_=0;
    freeCnt_=0;
}

SqlConnPool::~SqlConnPool() { ClosePool();}

SqlConnPool* SqlConnPool::Instance()
{
    static SqlConnPool connPool;
    return &connPool;
}

MYSQL* SqlConnPool::GetConn()
{
    MYSQL* sql=nullptr;
    if(connQue_.empty())
    {
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    sem_wait(&semId_);
    {
        lock_guard<mutex> locker(mtx_);
        sql=connQue_.front();
        connQue_.pop();
    }
    return sql;
}

void SqlConnPool::FreeConn(MYSQL* conn)
{
    assert(conn);
    lock_guard<mutex> locker(mtx_);
    connQue_.push(conn);
    sem_post(&semId_);
}

int SqlConnPool::GetFreeConnCnt()
{
    lock_guard<mutex> locker(mtx_);
    return connQue_.size();
}

void SqlConnPool::Init(const char* host,int port,const char* user,
          const char* pwd,const char* dbName,int connSize)
{
    assert(connSize>0);
    for(int i=0;i<connSize;i++)
    {
        MYSQL* sql=nullptr;
        sql=mysql_init(sql);
        if(!sql)
        {
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        sql=mysql_real_connect(sql,host,user,pwd,dbName,port,nullptr,0);
        if(!sql) LOG_ERROR("MySql connect error!");
        connQue_.push(sql);
        MAX_CONN_=connSize;
        sem_init(&semId_,0,MAX_CONN_);
    }
}

void SqlConnPool::ClosePool()
{
    lock_guard<mutex> locker(mtx_);
    while(!connQue_.empty())
    {
        auto item=connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    mysql_library_end();
}