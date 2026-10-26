#pragma once 

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <memory>
#include <string>

class SSLContext {
private:
    struct SSLCTXDeleter {
        void operator()(SSL_CTX* ctx) const {
            if (ctx) {
                SSL_CTX_free(ctx);
            }
        }
    };
    
    std::unique_ptr<SSL_CTX, SSLCTXDeleter> ctx_;
    bool initialized_ = false;
    std::string lastError_;

    SSLContext() = default;

public:
    SSLContext(const SSLContext&) = delete;
    SSLContext& operator=(const SSLContext&) = delete;

    static SSLContext& GetInstance();

    bool Initialize(const char* certPath,
        const char* keyPath,
        const char* caPath = nullptr,
        bool requireClientCertificate = false);

    bool Initialize(const std::string& certPath,
        const std::string& keyPath,
        const std::string& caPath = "",
        bool requireClientCertificate = false);

    SSL_CTX* GetContext() const { return ctx_.get(); }
    const std::string& GetLastError() const { return lastError_; }
};
