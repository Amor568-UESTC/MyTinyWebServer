#include <openssl/ssl.h>

#include "httpconn.h"

class HttpsConn : public HttpConn {
private:
    SSL* ssl_ = nullptr;
    bool sslHandShakeDone_ = false;

public:
    HttpsConn();
    ~HttpsConn() override;

    bool InitSSL();
    bool SSLHandShake();

    void Init(int sockFd,const sockaddr_in& addr) override;
    ssize_t read(int* saveErrno) override;
    ssize_t write(int* saveErrno) override;
    void Close() override;

    bool isHandShakeDone() const { return sslHandShakeDone_; }
};

// HttpsConn logs must include fd, ip, port...
#define LOG_CONN(level, conn, format, ...) \
    LOG_##level("client[%d](%s:%d) " format, \
        conn->GetFd(), conn->GetIP(), conn->GetPort(), ##__VA_ARGS__)