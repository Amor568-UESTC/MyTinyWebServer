#include"httpconn.h"

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
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d",
        fd_,GetIP(),GetPort(),(int)userCnt);
}

ssize_t HttpConn::read(int* saveErrno)
{
    ssize_t len=-1;
    do
    {
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len<=0) break;
    } while (isET);
    return len;
}

ssize_t HttpConn::write(int* saveErrno)
{
    ssize_t len=-1;
    do
    {
        len=writev(fd_,iov_,iovCnt_);
        if(len<=0)
        {
            *saveErrno=errno;
            break;
        }
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
    return len;
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
    request_.Init();
    if(readBuff_.ReadableBytes()<=0)
        return 0;
    else if(request_.parse(readBuff_))
    {
        LOG_DEBUG("%s",request_.path().c_str());
        response_.Init(srcDir,request_.path(),request_.IsKeepAlive(),200);
    }
    else response_.Init(srcDir,request_.path(),0,400);

    response_.MakeResponse(writeBuff_);
    iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len=writeBuff_.ReadableBytes();
    iovCnt_=1;

    if(response_.FileLen()>0&&response_.File())
    {
        iov_[1].iov_base=response_.File();
        iov_[1].iov_len=response_.FileLen();
        iovCnt_=2;
    }

    LOG_DEBUG("filesize:%d %d to %d",response_.FileLen(),iovCnt_,ToWriteBytes());
    return 1;
}