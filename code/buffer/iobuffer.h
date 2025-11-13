#include <openssl/ssl.h>

#include "buffer.h"

// for dev
#define OPENSSL_FOUND

class IOBuffer : public Buffer {
public:
    IOBuffer(int initBufferSize = 1024) : Buffer(initBufferSize) {}
    ~IOBuffer() = default;

    ssize_t ReadFd(int fd, int* saveErrno);
    ssize_t WriteFd(int fd, int* saveErrno);

#ifdef OPENSSL_FOUND
    ssize_t ReadSSL(SSL* ssl, int* saveErrno);
    ssize_t WriteSSL(SSL* ssl, int* saveErrno);
#endif
};