#pragma once

#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>

#include "epoller.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../http/httpconn.h"
#include "../http/httpsconn.h"
#include "../http/sslContext.h"
#include "../config/serverconfig.h"


class WebServer
{
private:
    bool initSocket_();
    bool initSSL_(const char* certPath, const char* keyPath, const char* caPath);
    void InitEventMode_(TriggerMode listenMode, TriggerMode connectionMode);
    void AddClient_(int fd, sockaddr_in addr);

    void DealListen_();
    void DealWrite_(HttpConn* client);
    void DealRead_(HttpConn* client);

    void SendError_(int fd, const char* info);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess(HttpConn* client);
    void OnHandShake(HttpsConn* client);

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    //使用配置集中原有的多个变量
    const ServerConfig config_;

    bool isClose_;
    int listenFd_;

    uint32_t listenEvent_;
    uint32_t connEvent_;

    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, std::unique_ptr<HttpConn>> user_;

public:
    explicit WebServer(ServerConfig config);

    ~WebServer();
    void Start();
};
