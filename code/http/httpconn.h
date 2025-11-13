#pragma once

#include<sys/types.h>
#include<sys/uio.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<errno.h>
#ifdef OPENSSL_FOUND
#include <openssl/ssl.h>
#endif

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

#ifdef OPENSSL_FOUND
    SSL* ssl_ = nullptr;
    bool isSSL_ = false;
    bool sslHandShakeDone_ = false;
#endif
    
public:
    HttpConn();
    ~HttpConn();

    void Init(int sockFd,const sockaddr_in& addr);
    ssize_t read(int* saveErrno); // to be changed
    ssize_t write(int* saveErrno); // to be changed
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

#ifdef OPENSSL_FOUND
    bool InitSSL();
    bool SSLHandShake();

    ssize_t SSLRead(int* saveErrno);
    ssize_t SSLWrite(int* saveErrno);

    bool isSSL() const {return isSSL_;}
    bool isSSLHandShakeDone() const {return sslHandShakeDone_;}
#endif
};