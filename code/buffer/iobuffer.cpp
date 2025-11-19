#include "iobuffer.h"

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
ssize_t IOBuffer::ReadSSL(SSL* ssl, int* saveErrno) {
    if (!ssl) {
        if (saveErrno) {
            *saveErrno = EBADF;
        }
        return -1;
    }

    char buff[65535];
    size_t writeble = WritableBytes();
    ssize_t totalLen = 0;

    // First read into the buffer's writable space
    if (writeble > 0) {
        ssize_t n = SSL_read(ssl, BeginWrite(), static_cast<int>(writeble));
        if (n > 0) {
            HasWritten(n);
            totalLen += n;
        } else if (n < 0) {
            if (saveErrno) {
                *saveErrno = SSL_get_error(ssl, static_cast<int>(n));
            }
        }
    }

    // Then read into the temporary buffer if needed
    ssize_t n = SSL_read(ssl, buff, sizeof(buff));
    if (n > 0) {
        Append(buff, n);
        totalLen += n;
    } else if (n < 0) {
        if (saveErrno) {
            *saveErrno = SSL_get_error(ssl, static_cast<int>(n));
        }
    }

    return totalLen;
}

ssize_t IOBuffer::WriteSSL(SSL* ssl, int* saveErrno) {
    if (!ssl) {
        if (saveErrno) {
            *saveErrno = EBADF;
        }
        return -1;
    }

    ssize_t readable = ReadableBytes();
    if (readable == 0) {
        return 0;   
    }

    ssize_t n = SSL_write(ssl, Peek(), static_cast<int>(readable));
    if (n > 0) {
        Retrieve(n);
    } else {
        if (saveErrno) {
            *saveErrno = SSL_get_error(ssl, static_cast<int>(n));
        }
    }
    return n;
}

#endif