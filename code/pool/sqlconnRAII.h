#pragma once

#include"sqlconnpool.h"

class SqlConnRAII
{
private:
    MYSQL* sql_;
    SqlConnPool* connpool_;

public:
    SqlConnRAII(MYSQL** sql,SqlConnPool* connpool)
    {
        assert(connpool);
        *sql=connpool->GetConn();
        sql_=*sql;
        connpool_=connpool;
    }

    ~SqlConnRAII() 
    {
        if(sql_) 
            connpool_->FreeConn(sql_);
    }
};