#include "webserver.h"

#include <utility>

using namespace std;

bool WebServer::initSocket_()
{
    int ret;
    sockaddr_in addr;
    if (config_.server.port > 65535 || config_.server.port < 1024) {
        LOG_ERROR("Port:%d error!", config_.server.port);
        return 0;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(config_.server.port);
    linger optLinger = {0};
    if (config_.server.linger) {
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!");
        return 0;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!");
        return 0;
    }

    int optval = 1;
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error!");
        close(listenFd_);
        return 0;
    }

    ret = bind(listenFd_, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("Bind Port:%d error!", config_.server.port);
        close(listenFd_);
        return 0;
    }

    ret = listen(listenFd_, 6);
    if (ret < 0) {
        LOG_ERROR("Listen Port:%d error!", config_.server.port);
        close(listenFd_);
        return 0;
    }

    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return 0;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Add listen error!");
    return 1;
}

bool WebServer::initSSL_(const char* certPath, const char* keyPath, const char* caPath)
{
    return SSLContext::GetInstance().Initialize(certPath, keyPath, caPath);
}

void WebServer::InitEventMode_(TriggerMode listenMode, TriggerMode connectionMode)
{
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    if (listenMode == TriggerMode::ET) {
        listenEvent_ |= EPOLLET;
    }
    if (connectionMode == TriggerMode::ET) {
        connEvent_ |= EPOLLET;
    }
    HttpConn::isET = (connEvent_ & EPOLLET) != 0;
}

void WebServer::AddClient_(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    if (config_.tls.enabled) {
        user_[fd] = make_unique<HttpsConn>();
    } else {
        user_[fd] = make_unique<HttpConn>();
    }
    if (SetFdNonblock(fd) < 0) {
        close(fd);
        return;
    }

    auto* client = user_[fd].get();
    client->Init(fd, addr);

    if (config_.tls.enabled) {
        auto* https = dynamic_cast<HttpsConn*>(client);
        if (!https || !https->InitSSL()) {
            client->Close();
            return;
        }
    }

    if (!epoller_->AddFd(fd, connEvent_ | EPOLLIN)) {
        client->Close();
        return;
    }

    // 全部成功之后再添加定时器
    if (config_.server.idleTimeoutMs > 0) {
        timer_->add(fd, config_.server.idleTimeoutMs, bind(&WebServer::CloseConn_, this, client));
    }

    LOG_INFO("Client[%d] in!", user_[fd]->GetFd());
}

void WebServer::DealListen_()
{
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (sockaddr*)&addr, &len);
        if (fd <= 0) {
            return;
        } else if (HttpConn::userCnt >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Client is full!");
            return;
        }
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET);
}

void WebServer::DealWrite_(HttpConn* client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnWrite_, this, client));
}

void WebServer::DealRead_(HttpConn* client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnRead_, this, client));
}

void WebServer::SendError_(int fd, const char* info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::ExtentTime_(HttpConn* client)
{
    assert(client);
    if (config_.server.idleTimeoutMs > 0) {
        timer_->adjust(client->GetFd(), config_.server.idleTimeoutMs);
    }
}

void WebServer::CloseConn_(HttpConn* client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

void WebServer::OnRead_(HttpConn* client)
{
    assert(client);
    //如果传入的是HttpConn就会为空, 走原逻辑
    auto* https = dynamic_cast<HttpsConn*>(client);
    if (https && !https->isHandShakeDone()) {
        OnHandShake(https);
        return;
    }
    ssize_t ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);

    // 优先对https进行状态检查和挂起操作处理, 避免在握手未完成时进行HTTP处理
    if (https) {
        using State = HttpsConn::IOState;
        using Op = HttpsConn::PendingOp;

        State state = https->GetIOState();
        if (state == State::Failed) {
            CloseConn_(client);
            return;
        }
        //状态为关闭可能是读端关闭了
        if (state == State::Closed) {
            OnProcess(client);
            return;
        }

        if (https->GetPendingOp() != Op::None) {
            uint32_t event;
            if (state == State::WantRead) {
                event = EPOLLIN;
            } else if (state == State::WantWrite) {
                event = EPOLLOUT;
            } else {
                // 有待恢复的操作, 却没有等待方向, 说明状态不一致
                CloseConn_(client);
                return;
            }

            if (!epoller_->ModFd(client->GetFd(), connEvent_ | event)) {
                CloseConn_(client);
            }
            return;
        }
    }

    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

void WebServer::OnWrite_(HttpConn* client)
{
    assert(client);
    auto* https = dynamic_cast<HttpsConn*>(client);
    if (https && !https->isHandShakeDone()) {
        OnHandShake(https);
        return;
    }
    ssize_t ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);

    if (https) {
        using State = HttpsConn::IOState;
        using Op = HttpsConn::PendingOp;

        State state = https->GetIOState();
        if (state == State::Failed || state == State::Closed) {
            CloseConn_(client);
            return;
        }

        if (https->GetPendingOp() != Op::None) {
            uint32_t event;
            if (state == State::WantRead) {
                event = EPOLLIN;
            } else if (state == State::WantWrite) {
                event = EPOLLOUT;
            } else {
                // 有待恢复的操作, 却没有等待方向, 说明状态不一致
                CloseConn_(client);
                return;
            }

            if (!epoller_->ModFd(client->GetFd(), connEvent_ | event)) {
                CloseConn_(client);
            }
            return;
        }
    }

    //在https外检查判断
    if (client->ToWriteBytes() > 0) {
        const bool retryable = writeErrno == EAGAIN ||
            writeErrno == EWOULDBLOCK || writeErrno == EINTR;
        if ((ret > 0 && writeErrno == 0) || retryable) {
            if (!epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT)) {
                CloseConn_(client);
            }
            return;
        } else {
            CloseConn_(client);
        }
        return;
    }

    if (client->ToWriteBytes() == 0) {
        if (client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

void WebServer::OnProcess(HttpConn* client)
{
    if (client->process()) {
        if (!epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT)) {
            CloseConn_(client);
        }
    } else if (client->IsReadClose()) {
        //请求不完整且不会在受到更多数据
        CloseConn_(client);
    } else if (!epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN)) {
        CloseConn_(client);
    }
}

void WebServer::OnHandShake(HttpsConn* client)
{
    using res = HttpsConn::HandShakeRes;

    uint32_t waitEvent = 0;

    switch (client->SSLHandShake()) {
        case res::Done:
            //握手成功, 直接尝试读取HTTP请求
            OnRead_(client);
            return;
        case res::WantRead:
            waitEvent = EPOLLIN;
            break;
        case res::WantWrite:
            waitEvent = EPOLLOUT;
            break;
        case res::Failed:
            CloseConn_(client);
            return;
    }
    //connEvent_保留EPOLLONESHOT, ET等配置
    if (!epoller_->ModFd(client->GetFd(), connEvent_ | waitEvent)) {
        CloseConn_(client);
    }
}

int WebServer::SetFdNonblock(int fd)
{
    assert(fd > 0);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

WebServer::WebServer(ServerConfig config)
    : config_(std::move(config)),
      isClose_(false),
      listenFd_(-1),
      listenEvent_(0),
      connEvent_(0),
      timer_(std::make_unique<HeapTimer>()),
      threadpool_(std::make_unique<ThreadPool>(config_.server.workerThreads)),
      epoller_(std::make_unique<Epoller>())
{
    HttpConn::userCnt = 0;
    HttpConn::srcDir = config_.server.documentRoot.c_str();

    const auto& db = config_.database;
    SqlConnPool::Instance()->Init(db.host.c_str(), db.port, db.user.c_str(),
                                db.password.c_str(), db.name.c_str(), db.poolSize);
    InitEventMode_(config_.server.listenMode, config_.server.connectionMode);
    if (!initSocket_()) {
        isClose_ = 1;
    }

    const auto& tls = config_.tls;
    if (tls.enabled) {
        if (tls.certificate.empty() || tls.privateKey.empty()) {
            isClose_ = true;
        } else if (!initSSL_(tls.certificate.c_str(), tls.privateKey.c_str(),
                            tls.clientCaFile.empty() ? nullptr : tls.clientCaFile.c_str())) {
            isClose_ = true;
        }
    }

    const auto& logging = config_.logging;
    if (logging.enabled) {
        Log::Instance()->init(logging.level, "./log", ".log", logging.asyncQueueSize);
        if (isClose_) {
            LOG_ERROR("========== Server init error!==========");
        } else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", config_.server.port,
                     config_.server.linger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     listenEvent_ & EPOLLET ? "ET" : "LT",
                     connEvent_ & EPOLLET ? "ET" : "LT");
            LOG_INFO("LogSys level: %d", logging.level);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d",
                     db.poolSize, config_.server.workerThreads);
        }
    }
}

WebServer::~WebServer()
{
    close(listenFd_);
    isClose_ = 1;
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::Start()
{
    int timeMS = -1;
    if (!isClose_) {
        LOG_INFO("========== Server start ==========");
    }
    while (!isClose_) {
        if (config_.server.idleTimeoutMs > 0) {
            timeMS = timer_->GetNextTick();
        }
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++) {
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if (fd == listenFd_) {
                DealListen_();
            } else if (events & (EPOLLHUP | EPOLLERR)) {
                assert(user_.count(fd));
                CloseConn_(user_[fd].get());
            } else if (events & (EPOLLIN | EPOLLOUT | EPOLLRDHUP)) {
                assert(user_.count(fd));

                HttpConn* client = user_[fd].get();
                auto* https = dynamic_cast<HttpsConn*>(client);
                if (https && https->GetPendingOp() == HttpsConn::PendingOp::Read) {
                    DealRead_(client);
                } else if (https && https->GetPendingOp() == HttpsConn::PendingOp::Write) {
                    DealWrite_(client);
                } else if ((events & EPOLLOUT) && client->ToWriteBytes() > 0) {
                    DealWrite_(client); //优先发送已有的响应
                } else if (events & (EPOLLIN | EPOLLRDHUP)) {
                    DealRead_(client);
                } else if (events & EPOLLOUT) {
                    DealWrite_(client);
                }
            }
        }
    }
}
