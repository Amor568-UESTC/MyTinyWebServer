#include"httpconn.h"

#include "../handler/requesthandler.h"

using namespace std;

const char* HttpConn::srcDir;
atomic<int> HttpConn::userCnt;
bool HttpConn::isET;

HttpConn::HttpConn()
{
    fd_=-1;
    addr_={0};
    isClose_=1;
}

HttpConn::~HttpConn() { Close();}

void HttpConn::Init(int sockFd,const sockaddr_in& addr)
{
    assert(sockFd>0);
    userCnt++;
    addr_=addr;
    fd_=sockFd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_=0;
    //初始化iovec
    iovCnt_ = 0;
    iov_[0] = {};
    iov_[1] = {};
    request_.Init();
    requestDone_ = false;
    keepAlive_ = false;

    LOG_INFO("Client[%d](%s:%d) in, userCount:%d.",
        fd_,GetIP(),GetPort(),(int)userCnt);
}

ssize_t HttpConn::read(int* saveErrno)
{
    ssize_t totalLen=0;
    ssize_t len=-1;
    do
    {
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len>0) totalLen+=len;
        else break;
    } while (isET);
    return totalLen>0?totalLen:len;
}

ssize_t HttpConn::write(int* saveErrno)
{
    ssize_t totalLen=0;
    ssize_t len=-1;
    do
    {
        len=writev(fd_,iov_,iovCnt_);
        if(len<=0)
        {
            *saveErrno=errno;
            break;
        }
        totalLen+=len;
        if(iov_[0].iov_len+iov_[1].iov_len==0)
            break;
        else if(static_cast<size_t>(len)>iov_[0].iov_len)
        {
            iov_[1].iov_base=(uint8_t*)iov_[1].iov_base+(len-iov_[0].iov_len);
            iov_[1].iov_len-=(len-iov_[0].iov_len);
            if(iov_[0].iov_len)
            {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len=0;
            }
        }
        else
        {
            iov_[0].iov_base=(uint8_t*)iov_[0].iov_base+len;
            iov_[0].iov_len-=len;
            writeBuff_.Retrieve(len);
        }
    } while (isET||ToWriteBytes()>10240);
    return totalLen>0?totalLen:len;
}

void HttpConn::Close()
{
    response_.UnmapFile();
    if(isClose_==0)
    {
        isClose_=1;
        userCnt--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d",
            fd_,GetIP(),GetPort(),(int)userCnt);
    }
}

int HttpConn::GetFd() const { return fd_;}

int HttpConn::GetPort() const { return addr_.sin_port;}

const char* HttpConn::GetIP() const { return inet_ntoa(addr_.sin_addr);}

sockaddr_in HttpConn::GetAddr() const { return addr_;}

bool HttpConn::process()
{
    if (requestDone_) {
        request_.Init();
        requestDone_ = false;
    }

    const auto res = request_.parse(readBuff_);
    if (res == HttpRequest::ParseRes::NeedMore) {
        return false; //OnProcess()会重新监听EPOLLIN
    }

    requestDone_ = true;
    const bool complete = res == HttpRequest::ParseRes::Complete;
    const bool canContinue = !IsReadClose() || readBuff_.ReadableBytes() > 0;
    keepAlive_ = complete && request_.IsKeepAlive() && canContinue;
    
    if (!complete) {
        response_.Init(srcDir, "/400.html", false, 400);
        response_.MakeResponse(writeBuff_);
    } else {
        const RequestHandler handler;
        const auto result = handler.Handle(request_);
        if (result.kind == HandlerResult::Kind::Text) {
            response_.MakeTextResponse(writeBuff_, result.body, result.contentType, keepAlive_, result.status);
        } else {
            response_.Init(srcDir, result.path, keepAlive_, result.status);
            response_.MakeResponse(writeBuff_);
        }
    }
    iov_[0] = {}, iov_[1] = {}; //清空向量
    iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len=writeBuff_.ReadableBytes();
    iovCnt_=1;

    if(response_.FileLen()>0&&response_.File())
    {
        iov_[1].iov_base=response_.File();
        iov_[1].iov_len=response_.FileLen();
        iovCnt_=2;
    }

    LOG_DEBUG("filesize:%zu %d to %zu",response_.FileLen(),iovCnt_,ToWriteBytes());
    return 1;
}
