#pragma once

#include<sys/types.h>
#include<sys/uio.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<errno.h>

#include"../log/log.h"
#include"../buffer/iobuffer.h"
#include"httprequest.h"
#include"httpresponse.h"

class HttpConn
{
private:
    int fd_;
    sockaddr_in addr_;

    bool isClose_;

    HttpRequest request_;
    HttpResponse response_;

    //增加一个标志位，表示请求是否已经完成解析
    bool requestDone_ = false;
    bool keepAlive_ = false;

protected:
    IOBuffer readBuff_;
    IOBuffer writeBuff_;

    int iovCnt_ = 0;
    iovec iov_[2]{};
    
public:
    HttpConn();
    virtual ~HttpConn();

    virtual void Init(int sockFd,const sockaddr_in& addr);
    virtual ssize_t read(int* saveErrno);
    virtual ssize_t write(int* saveErrno);
    virtual void Close();
    virtual bool IsReadClose() const { return false; }

    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    bool process();

    size_t ToWriteBytes() {return iov_[0].iov_len+iov_[1].iov_len;}
    bool IsKeepAlive() const { return keepAlive_; } //使用自己的变量来判断

    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCnt;
};
