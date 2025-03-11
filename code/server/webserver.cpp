#include"webserver.h"

using namespace std;

bool WebServer::initSocket_()
{
    int ret;
    sockaddr_in addr;
    if(port_>65535||port_<1024)
    {
        LOG_ERROR("Port:%d error!",port_);
        return 0;
    }
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(port_);
    linger optLinger={0};
    if(openLinger_)
    {
        optLinger.l_onoff=1;
        optLinger.l_linger=1;
    }

    listenFd_=socket(AF_INET,SOCK_STREAM,0);
    if(listenFd_<0)
    {
        LOG_ERROR("Create socket error!");
        return 0;
    }

    ret=setsockopt(listenFd_,SOL_SOCKET,SO_LINGER,&optLinger,sizeof(optLinger));
    if(ret<0)
    {
        close(listenFd_);
        LOG_ERROR("Init linger error!");
        return 0;
    }

    int optval=1;
    ret=setsockopt(listenFd_,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));
    if(ret==-1)
    {
        LOG_ERROR("set socket setsockopt error!");
        close(listenFd_);
        return 0;
    }

    ret=bind(listenFd_,(sockaddr*)&addr,sizeof(addr));
    if(ret<0)
    {
        LOG_ERROR("Bind Port:%d error!",port_);
        close(listenFd_);
        return 0;
    }

    ret=listen(listenFd_,6);
    if(ret<0)
    {
        LOG_ERROR("Listen Port:%d error!",port_);
        close(listenFd_);
        return 0;
    }

    ret=epoller_->AddFd(listenFd_,listenEvent_|EPOLLIN);
    if(ret==0)
    {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return 0;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Add listen error!");
    return 1;
}

void WebServer::InitEventMode_(int trigMode)
{
    listenEvent_=EPOLLRDHUP;
    connEvent_=EPOLLONESHOT|EPOLLRDHUP;
    switch(trigMode)
    {
        case 0:
            break;
        case 1:
            connEvent_|=EPOLLET;
            break;
        case 2:
            listenEvent_|=EPOLLET;
            break;
        default:
            listenEvent_|=EPOLLET;
            connEvent_|=EPOLLET;
            break;
    }
    HttpConn::isET=(connEvent_&EPOLLET);
}

void WebServer::AddClient_(int fd,sockaddr_in addr)
{
    assert(fd>0);
    user_[fd].Init(fd,addr);
    if(timeoutMS_>0)
        timer_->add(fd,timeoutMS_,bind(&WebServer::CloseConn_,this,&user_[fd]));
    epoller_->AddFd(fd,EPOLLIN|connEvent_);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!",user_[fd].GetFd());
}

void WebServer::DealListen_()
{
    sockaddr_in addr;
    socklen_t len=sizeof(addr);
    do
    {
        int fd=accept(listenFd_,(sockaddr*)&addr,&len);
        if(fd<=0) return ;
        else if(HttpConn::userCnt>=MAX_FD)
        {
            SendError_(fd,"Server busy!");
            LOG_WARN("Client is full!");
            return ;
        }
        AddClient_(fd,addr);
    }while(listenEvent_&EPOLLET);
}

void WebServer::DealWrite_(HttpConn* client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnWrite_,this,client));
    
}

void WebServer::DealRead_(HttpConn* client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnRead_,this,client));
}

void WebServer::SendError_(int fd,const char* info)
{
    assert(fd>0);
    int ret=send(fd,info,strlen(info),0);
    if(ret<0)
        LOG_WARN("send error to client[%d] error!",fd);
    close(fd);
}

void WebServer::ExtentTime_(HttpConn* client)
{
    assert(client);
    if(timeoutMS_>0) timer_->adjust(client->GetFd(),timeoutMS_);
}

void WebServer::CloseConn_(HttpConn* client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!",client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

void WebServer::OnRead_(HttpConn* client)
{
    assert(client);
    int ret=-1;
    int readErrno=0;
    ret=client->read(&readErrno);
    if(ret<=0&&readErrno!=EAGAIN)
    {
        CloseConn_(client);
        return ;
    }
    OnProcess(client);
}

void WebServer::OnWrite_(HttpConn* client)
{
    assert(client);
    int ret=-1;
    int writeErrno=0;
    ret=client->write(&writeErrno);
    if(client->ToWriteBytes()==0)
    {
        if(client->IsKeepAlive())
        {
            OnProcess(client);
            return ;
        }
    }
    else if(ret<0)
    {
        if(writeErrno==EAGAIN)
        {
            epoller_->ModFd(client->GetFd(),connEvent_|EPOLLOUT);
            return ;
        }
    }
    CloseConn_(client);
}

void WebServer::OnProcess(HttpConn* client)
{
    if(client->process())
        epoller_->ModFd(client->GetFd(),connEvent_|EPOLLOUT);
    else
        epoller_->ModFd(client->GetFd(),connEvent_|EPOLLIN);
}

int WebServer::SetFdNonblock(int fd)
{
    assert(fd>0);
    return fcntl(fd,F_SETFL,fcntl(fd,F_GETFD,0)|O_NONBLOCK);
}

WebServer::WebServer(
    int port,int trigMode,int timeoutMS,bool OptLinger,
    int sqlPort,const char* sqlUser,const char* sqlPwd,const char* dbName,
    int connPoolNum,int threadNum,bool openLog,int logLevel,int logQueSize
):  port_(port),openLinger_(OptLinger),timeoutMS_(timeoutMS),isClose_(0),
    timer_(new HeapTimer()),threadpool_(new ThreadPool(threadNum)),epoller_(new Epoller())
{
    srcDir_=getcwd(nullptr,256);
    assert(srcDir_);
    strncat(srcDir_,"/rss/",16);
    HttpConn::userCnt=0;
    HttpConn::srcDir=srcDir_;
    SqlConnPool::Instance()->Init("localhost",sqlPort,sqlUser,sqlPwd,dbName,connPoolNum);
    InitEventMode_(trigMode);
    if(!initSocket_()) isClose_=1;
    if(openLog)
    {
        Log::Instance()->init(logLevel,"./log",".log",logQueSize);
        if(isClose_) LOG_ERROR("========== Server init error!==========");
        else
        {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s",port_,OptLinger?"true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                        listenEvent_&EPOLLET?"ET":"LT",
                        connEvent_&EPOLLET?"ET":"LT");
            LOG_INFO("LogSys level: %d",logLevel);
            LOG_INFO("srcDir: %s",HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d",connPoolNum,threadNum);
        }
    }
}

WebServer::~WebServer()
{
    close(listenFd_);
    isClose_=1;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::Start()
{
    int timeMS=-1;
    if(!isClose_) LOG_INFO("========== Server start ==========");
    while(!isClose_)
    {
        if(timeoutMS_>0) timeMS=timer_->GetNextTick();
        int eventCnt=epoller_->Wait(timeMS);
        for(int i=0;i<eventCnt;i++)
        {
            int fd=epoller_->GetEventFd(i);
            uint32_t events=epoller_->GetEvents(i);
            if(fd==listenFd_) DealListen_();
            else if(events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                assert(user_.count(fd));
                CloseConn_(&user_[fd]);
            }
            else if(events&EPOLLIN)
            {
                assert(user_.count(fd));
                DealRead_(&user_[fd]);
            }
            else if(events&EPOLLOUT)
            {
                assert(user_.count(fd));
                DealWrite_(&user_[fd]);
            }
            else LOG_ERROR("Unexpected event");
        }
    }
}