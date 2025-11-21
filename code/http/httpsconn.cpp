#include "httpsconn.h"
#include "sslcontext.h"

bool HttpConn::InitSSL() 
{
    if (ssl_) {
        LOG_CONN(WARN, this, "SSL already initialized");
        return true;
    }

    SSLContext& sslCtx = SSLContext::GetInstance();
    SSL_CTX* ctx = sslCtx.GetContext();
    if (!ctx) {
        LOG_ERROR("SSL context not available");
        return false;
    }

    ssl_ = SSL_new(ctx);
    if (!ssl_) {
        LOG_CONN(ERROR, this, "SSL_new failed");
        return false;
    }

    // associate ssl_ with fd_
    if (!SSL_set_fd(ssl_, fd_)) {
        LOG_CONN(ERROR, this, "SSL_set_fd failed");
        SSL_free(ssl_);
        ssl = nullptr;
        return false;
    }

    // server mode to handle shake auto
    SSL_set_accept_state(ssl_);

    sslHandShakeDone_ = false;

    LOG_CONN(DEBUG, this, "SSL initialized");
    return true;
}

bool HttpConn::SSLHandShake()
{
    if (!ssl_) {
        LOG_CONN(ERROR, this, "SSL not initialized for handshake");
        return false;
    }

    if (sslHandShakeDone_) {
        LOG_CONN(WARN, this, "SSL handshake already done");
        return true;
    }

    if (SSL_do_handshake(ssl_) == 1) {
        sslHandShakeDone_ = true;

        // print handshake info
        const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
        if (cipher) {
            LOG_CONN(INFO, this, "SSL handshake completed. Cipher: %s", SSL_CIPHER_get_cipher_name(cipher));
        }
        return true;
    } else {
        int sslErr = SSL_get_error(ssl_, ret);
        switch(sslErr) {
            case SSL_ERROR_WANT_READ:
                LOG_CONN(DEBUG, this, "SSL handshake needs more read");
                break;
            case SSL_ERROR_WANT_WRITE:
                LOG_CONN(DEBUG, this, "SSL handshake needs more write");
                break;
            case SSL_ERROR_SYSCALL:
                LOG_CONN(DEBUG, this, "SSL handshake system error: %s",strerror(errno));
                break;
            case SSL_ERROR_SSL:
                {
                    char errBuf[256];
                    ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
                    LOG_CONN(ERROR, this, "SSL handshake failed with error: %s", errBuf);
                }
                break;
            default:
                LOG_CONN(ERROR, this, "SSL shakehand with error: %d", sslErr);
                break;
        }
        return false;
    }
}

// separate construction from init
HttpsConn::HttpsConn() : HttpConn() {}

HttpsConn::~HttpsConn() {
    Close();
}

void HttpsConn::Init(int sockFd,const sockaddr_in& addr) : HttpConn::Init(sockFd, addr) {
    InitSSL();
}

ssize_t HttpsConn::read(int* saveErrno) {
    if (!ssl_) {
        if (saveErrno) {
            *saveErrno = EBADF;
        }
        return -1;
    }

    if (!sslHandShakeDone_) {
        if (!SSLHandShake()) {
            if (saveErrno) {
                *saveErrno = EAGAIN;
            }
            return -1;
        }
    }

    ssize_t totalLen = 0;
    ssize_t len = -1;
    do {
        len = readBuff_.ReadSSL(ssl_, saveErrno);
        if (len > 0) totalLen += len;
        else {
            break;
        }
    } while (isET);

    return totalLen > 0 ? totalLen : len;
}

ssize_t HttpsConn::write(int* saveErrno) {
    if (!ssl_) {
        if (saveErrno) {
            *saveErrno = EBADF;
        }
        return -1;
    }

    if (!sslHandShakeDone_) {
        if (!SSLHandShake()) {
            if (saveErrno) {
                *saveErrno = EAGAIN;
            }
            return -1;
        }
    }

    ssize_t len = -1;
    // use of iov ? 
}
