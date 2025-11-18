#include<openssl/err.h>

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

void HttpConn::Init(int sockFd,const sockaddr_in& addr,bool isSSL)
{
    assert(sockFd>0);
    userCnt++;
    addr_=addr;
    fd_=sockFd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_=0;
#ifdef OPENSSL_FOUND
    isSSL_=isSSL;
    if(ssl_)
    {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_=nullptr;
    }
    if(isSSL_)
        if(!InitSSL())
        {
            LOG_ERROR("Failed to initialize SSL for fd: %d. Continue without SSL",fd_);
            isSSL_=false;
        }
#endif
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d. SSL: %s",
        fd_,GetIP(),GetPort(),(int)userCnt,isSSL_);
}

ssize_t HttpConn::read(int* saveErrno)
{
#ifdef OPENSSL_FOUND
    if(isSSL_) return SSLRead(saveErrno);
#endif
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
#ifdef OPENSSL_FOUND
    if(isSSL_) return SSLWrite(saveERRno);
#endif
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

#ifdef OPENSSL_FOUND
bool HttpConn::InitSSL() 
{
    if(!enableHttps_) return false;
    if(ssl_) 
    {
        LOG_WARN("SSL already initialized for this connection");
        return true;
    }

    SSLContext& sslCtx=SSLContext::GetInstance();
    SSL_CTX* ctx=sslCtx.GetContext();
    if(!ctx)
    {
        LOG_ERROR("SSL context not available");
        return false;
    }

    ssl_=SSL_new(ctx);
    if(!ssl)
    {
        LOG_ERROR("SSL_new failed");
        return false;
    }

    // associate ssl_ with fd_
    if(SSL_set_fd(ssl_,fd_)!=1) 
    {
        LOG_ERROR("SSL_set_fd failed");
        SSL_free(ssl_);
        ssl_=nullptr;
        return false;
    }

    // server mode to handle shake auto
    SSL_set_accept_state(ssl_);

    isSSL_=true;
    sslHandShakeDone_=false;

    LOG_DEBUG("SSL initialized for fd: %d",fd_);
    return true;
}

bool HttpConn::SSLHandShake()
{
    if(!SSL||!ssl_)
    {
        LOG_ERROR("SSL not initialized for handshake");
        return false;
    }

    if(sslHandShakeDone_) return true;

    int ret=SSL_do_handshake(ssl_);
    if(ret==1)
    {
        sslHandShakeDone_=true;

        // print handshake info
        const SSL_CIPHER* cipher=SSL_get_current_cipher(ssl_);
        if(cipher) LOG_INFO("SSL handshake completed. Cipher: %s",SSL_CIPHER_get_cipher_name(cipher));

        return true;
    }
    else
    {
        int sslError=SSL_get_error(ssl_,ret);
        switch(sslError)
        {
            case SSL_ERROR_WANT_READ:
                LOG_DEBUG("SSL handshake needs more read");
                break;
            case SSL_ERROR_WANT_WRITE:
                LOG_DEBUG("SSL handshake needs more write");
                break;
            case SSL_ERROR_SYSCALL:
                LOG_DEBUG("SSL handshake system error: %s",strerror(errno));
                break;
            case SSL_ERROR_SSL:
                {
                    char errBuf[256];
                    ERR_error_string_n(ERR_get_ERROR(),errBuf,sizeof(errBuf));
                    LOG_ERROR("SSL handshake failed with error: %d",errBuf);
                }
                break;
            default:
                LOG_ERROR("SSL shakehand with error: %d",sslError);
                break;
        }
        return false;
    }
}

ssize_t HttpConn::SSLRead(int* saveErrno)
{
    if(!isSSL_||!ssl_)
    {
        if(saveErrno) *saveErrno=EBADF;
        LOG_ERROR("SSL not initialized for read");
        return -1;
    }

    if(!sslHandShakeDone_)
        if(!SSLHandShake())
        {
            int sslError=SSL_get_error(ssl_,-1);
            if(saveErrno) *saveErrno=sslError;

            // error to need retry like EAGAIN
            if(sslError==SSL_ERROR_WANT_READ||sslError==SSL_ERROR_WANT_WRITE)
                return -1;
            else 
            {
                LOG_ERROR("SSL handshake failed in SSLRead");
                return -1;
            }
        }

    ssize_t len=-1;
    do
    {
        len=readBuff_.SSLReadFd(ssl_,saveErrno);
        if(len<=0) break;
    } while (isET);

    return len;
}

ssize_t HttpConn::SSLWrite(int* saveErrno)
{
    if(!isSSL_||!ssl_)
    {
        if(saveErrno) *saveErrno=EBADF;
        LOG_ERROR("SSL not initialized for write");
        return -1;
    }

    if(!sslHandShakeDone_)
        if(!SSLHandShake())
        {
            int sslError=SSL_get_error(ssl_,-1);
            if(saveErrno) *saveErrno=sslError;

            // error to need retry like EAGAIN
            if(sslError==SSL_ERROR_WANT_READ||sslError==SSL_ERROR_WANT_WRITE)
                return -1;
            else 
            {
                LOG_ERROR("SSL handshake failed in SSLWrite");
                return -1;
            }
        }

    return writeBuff_.SSLWriteFd(ssl_,saveErrno);
}



#endif