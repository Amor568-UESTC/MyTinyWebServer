#include "iobuffer.h"

#include <openssl/err.h>

ssize_t IOBuffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];
    iovec iov[2];
    size_t writable = WritableBytes();

    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    ssize_t n = readv(fd, iov, 2);
    if (n < 0) {
        if (saveErrno) {
            *saveErrno = errno;
        }
    } else if (static_cast<size_t>(n) <= writable) {
        HasWritten(n);
    } else {
        HasWritten(writable);
        Append(buff, n - writable);
    }
    return n;
}

ssize_t IOBuffer::WriteFd(int fd, int* saveErrno) {
    size_t readable = ReadableBytes();
    if (readable == 0) {
        return 0;
    }

    ssize_t n  = write(fd, Peek(), readable);
    if (n < 0) {
        if (saveErrno) {
            *saveErrno = errno;
        }
    } else {
        Retrieve(n);
    }

    return n;
}

#ifdef OPENSSL_FOUND
//走openSSL的错误而非原来的errno
ssize_t IOBuffer::ReadSSL(SSL* ssl, int* sslError) {
    if (sslError) {
        *sslError = SSL_ERROR_NONE;
    }

    if (!ssl) {
        if (sslError) {
            *sslError = SSL_ERROR_SSL;
        }
        return -1;
    }

    //固定长度读取
    constexpr int chunckSize = 16 * 1024;
    EnsureWritable(chunckSize);

    ERR_clear_error();
    errno = 0;

    int n = SSL_read(ssl, BeginWrite(), chunckSize);
    if (n > 0) {
        HasWritten(n);
        return n;
    }

    int error = SSL_get_error(ssl, n);
    if (sslError)  {
        *sslError = error;
    }

    return error == SSL_ERROR_ZERO_RETURN ? 0 : -1;
}

ssize_t IOBuffer::WriteSSL(SSL* ssl, int* sslError) {
    if (sslError) {
        *sslError = SSL_ERROR_NONE;
    }

    if (!ssl) {
        if (sslError) {
            *sslError = SSL_ERROR_SSL;
        }
        return -1;
    }

    size_t readable = ReadableBytes();
    if (readable == 0) {
        if (sslError) {
            *sslError = SSL_ERROR_NONE;
        }
        return 0;
    }

    ERR_clear_error();
    errno = 0;

    int n = SSL_write(ssl, Peek(), readable);
    if (n > 0) {
        Retrieve(n);
        return n;
    }

    int error = SSL_get_error(ssl, n);
    if (sslError) {
        *sslError = error; 
    }

    return error == SSL_ERROR_ZERO_RETURN ? 0 : -1;
}

#endif