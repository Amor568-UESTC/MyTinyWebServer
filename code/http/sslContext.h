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

    SSLContext() = default;

public:
    static SSLContext& GetInstance();

    bool Initialize(const std::string& certPaht, 
        const std::string& keyPath,
        const std::string& caPath);

    SSL_CTX* GetContext() const { return ctx_.get(); }
};