#include <openssl/ssl.h>

#include "httpconn.h"

class HttpsConn : public HttpConn {
private:
    SSL* ssl_ = nullptr;
    bool sslHandShakeDone_ = false;
    //IOState会在下一次读写时重置,需要单独记录
    bool readClosed_ = false;

public:
    HttpsConn();
    ~HttpsConn() override;

    bool InitSSL();

    enum class HandShakeRes {
        Done,       //握手完成可以处理HTTP
        WantRead,   //等待socket可读后继续握手
        WantWrite,  //等待socket可写后继续握手
        Failed      //握手失败关闭连接
    };
    HandShakeRes SSLHandShake();

    void Init(int sockFd,const sockaddr_in& addr) override;
    ssize_t read(int* sslError) override;
    ssize_t write(int* sslError) override;
    void Close() override;
    bool IsReadClose() const override { return readClosed_; }

    bool isHandShakeDone() const { return sslHandShakeDone_; }

public:
    //增加IO状态和挂起操作的概念, 方便在WebServer中处理SSL握手和IO事件
    enum class IOState {
        Ready,
        WantRead,
        WantWrite,
        Closed,
        Failed
    };

    enum class PendingOp {
        None,
        Read,
        Write
    };

private:
    IOState ioState_ = IOState::Ready;
    PendingOp pendingOp_ = PendingOp::None;

public:
    IOState GetIOState() const { return ioState_; }
    PendingOp GetPendingOp() const { return pendingOp_; }
};

// HttpsConn logs must include fd, ip, port...
#define LOG_CONN(level, conn, format, ...) \
    LOG_##level("client[%d](%s:%d) " format, \
        conn->GetFd(), conn->GetIP(), conn->GetPort(), ##__VA_ARGS__)
