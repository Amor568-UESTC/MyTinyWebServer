#pragma once

#include<unordered_map>
#include<fcntl.h>
#include<unistd.h>
#include<errno.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include"epoller.h"
#include"../timer/heaptimer.h"
#include"../pool/sqlconnpool.h"
#include"../pool/threadpool.h"
#include"../http/httpconn.h"



class WebServer
{
private:
    bool initSocket_();
    void InitEventMode_(int trigMode);
    void AddClient_(int fd,sockaddr_in addr);

    void DealListen_();
    void DealWrite_(HttpConn* client);
    void DealRead_(HttpConn* client);

    void SendError_(int fd,const char* info);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess(HttpConn* client);

    static const int MAX_FD=65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool openLinger_;
    int timeoutMS_;
    bool isClose_;
    int listenFd_;
    char* srcDir_;

    uint32_t listenEvent_;
    uint32_t connEvent_;

    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int,HttpConn> user_;

public:
    WebServer(
        int port,int trigMode,int timeoutMS,bool OptLinger,
        int sqlPort,const char* sqlUser,const char* sqlPwd,const char* dbName,
        int connPoolNum,int threadNum,bool openLog,int logLevel,int logQueSize
    );

    ~WebServer();
    void Start();
};