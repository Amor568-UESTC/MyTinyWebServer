#include "httpsconn.h"
#include "sslContext.h"

bool HttpsConn::InitSSL() {
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
    if (!SSL_set_fd(ssl_, GetFd())) {
        LOG_CONN(ERROR, this, "SSL_set_fd failed");
        SSL_free(ssl_);
        ssl_ = nullptr;
        return false;
    }

    // server mode to handle shake auto
    SSL_set_accept_state(ssl_);

    sslHandShakeDone_ = false;

    LOG_CONN(DEBUG, this, "SSL initialized");
    return true;
}

HttpsConn::HandShakeRes HttpsConn::SSLHandShake() {
    using res = HandShakeRes;

    if (!ssl_) {
        LOG_CONN(ERROR, this, "SSL not initialized for handshake");
        return res::Failed;
    }

    if (sslHandShakeDone_) {
        LOG_CONN(WARN, this, "SSL handshake already done");
        return res::Done;
    }

    ERR_clear_error();
    errno = 0;
    int ret = SSL_do_handshake(ssl_);
    if (ret == 1) {
        sslHandShakeDone_ = true;

        // print handshake info
        const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
        if (cipher) {
            LOG_CONN(INFO, this, "SSL handshake completed. Cipher: %s", SSL_CIPHER_get_name(cipher));
        }
        return res::Done;
    } else {
        int sslErr = SSL_get_error(ssl_, ret);
        switch(sslErr) {
            case SSL_ERROR_WANT_READ:
                LOG_CONN(DEBUG, this, "SSL handshake needs more read");
                return res::WantRead;
            case SSL_ERROR_WANT_WRITE:
                LOG_CONN(DEBUG, this, "SSL handshake needs more write");
                return res::WantWrite;
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
        return res::Failed;
    }
}

// separate construction from init
HttpsConn::HttpsConn() : HttpConn() {}

HttpsConn::~HttpsConn() {
    Close();
}

void HttpsConn::Init(int sockFd,const sockaddr_in& addr) {
    HttpConn::Init(sockFd, addr);
    
    // 先不InitSSL, 由WebServer统一驱动SSL握手, 避免在握手未完成时进行HTTP处理
    sslHandShakeDone_ = false;
    ioState_ = IOState::Ready;
    pendingOp_ = PendingOp::None;
}

ssize_t HttpsConn::read(int* sslError) {
    // 操作先清理上次结果
    ioState_ = IOState::Ready;
    pendingOp_ = PendingOp::None;
    if (sslError) {
        *sslError = SSL_ERROR_NONE;
    }

    // 握手由服务器统一驱动, read 专注于读逻辑
    // if (!sslHandShakeDone_) {
    //     if (!SSLHandShake()) {
    //         if (saveErrno) {
    //             *saveErrno = EAGAIN;
    //         }
    //         return -1;
    //     }
    // }

    ssize_t totalLen = 0;
    ssize_t len = -1;
    do {
        len = readBuff_.ReadSSL(ssl_, sslError);
        if (len > 0) {
            totalLen += len;
        } else {
            switch (*sslError) {
                case SSL_ERROR_WANT_READ:
                    ioState_ = IOState::WantRead;
                    pendingOp_ = PendingOp::Read;
                    break;
                case SSL_ERROR_WANT_WRITE:
                    ioState_ = IOState::WantWrite;
                    pendingOp_ = PendingOp::Read;
                    break;
                case SSL_ERROR_ZERO_RETURN:
                    ioState_ = IOState::Closed;
                    break;
                default:
                    ioState_ = IOState::Failed;
                    break;
            }
            break;
        }
    } while (isET);

    // 本轮已经读到数据, 随后因为等待更多数据而退出
    if (totalLen > 0 && ioState_ == IOState::WantRead) {
        pendingOp_ = PendingOp::None;
    }

    return totalLen > 0 ? totalLen : len;
}

ssize_t HttpsConn::write(int* sslError) {
    // 操作先清理上次结果
    ioState_ = IOState::Ready;
    pendingOp_ = PendingOp::None;
    if (sslError) {
        *sslError = SSL_ERROR_NONE;
    }

    // 握手由服务器统一驱动, write 专注于读逻辑
    // if (!sslHandShakeDone_) {
    //     if (!SSLHandShake()) {
    //         if (saveErrno) {
    //             *saveErrno = EAGAIN;
    //         }
    //         return -1;
    //     }
    // }

    ssize_t totalLen = 0;
    ssize_t len = -1;
    do {
        const iovec* curIov = &iov_[0];
        if (iov_[0].iov_len <= 0 && iovCnt_ > 1) {
            curIov = &iov_[1];
        }

        if (curIov->iov_len == 0) {
            break;
        }

        ERR_clear_error();
        errno = 0;
        len = SSL_write(ssl_, curIov->iov_base, curIov->iov_len);
        if (len > 0) {
            totalLen += len;

            if (curIov == &iov_[0]) {
                writeBuff_.Retrieve(len);
                iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + len;
                iov_[0].iov_len -= len;
            } else {
                iov_[1].iov_base = static_cast<char*>(iov_[1].iov_base) + len;
                iov_[1].iov_len -= len;
            }
        } else {
            int error = SSL_get_error(ssl_, len);
            switch (error) {
                case SSL_ERROR_WANT_READ:
                    ioState_ = IOState::WantRead;
                    pendingOp_ = PendingOp::Write;
                    break;
                case SSL_ERROR_WANT_WRITE:
                    ioState_ = IOState::WantWrite;
                    pendingOp_ = PendingOp::Write;
                    break;
                case SSL_ERROR_ZERO_RETURN:
                    ioState_ = IOState::Closed;
                    break;
                default:
                    ioState_ = IOState::Failed;
                    break;
            }
            if (sslError) {
                *sslError = error;
            }
            break;
        }

    } while (isET || ToWriteBytes() > 0);

    return totalLen > 0 ? totalLen : len;
}

void HttpsConn::Close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    HttpConn::Close();
}