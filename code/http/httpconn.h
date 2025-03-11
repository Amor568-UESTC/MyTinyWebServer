#pragma once

#include<sys/types.h>
#include<sys/uio.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<errno.h>

#include"../log/log.h"
#include"../pool/sqlconnRAII.h"
#include"../buffer/buffer.h"
#include"httprequest.h"
#include"httpresponse.h"

class HttpConn
{
private:
    int fd_;
    sockaddr_in addr_;

    bool isClose_;

    int iovCnt_;
    iovec iov_[2];

    Buffer readBuff_;
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;
    
public:
    HttpConn();
    ~HttpConn();

    void Init(int sockFd,const sockaddr_in& addr);
    ssize_t read(int* saveErrno);
    ssize_t write(int* saveErrno);
    void Close();

    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    bool process();

    int ToWriteBytes() {return iov_[0].iov_len+iov_[1].iov_len;}
    bool IsKeepAlive() const {return request_.IsKeepAlive();}

    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCnt;
};