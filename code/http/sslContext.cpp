#include "sslContext.h"
#include "../log/log.h"

SSLContext& SSLContext::GetInstance() {
    static SSLContext instance;
    return instance;
}

bool SSLContext::Initialize(const std::string& certPath, 
    const std::string& keyPath,
    const std::string& caPath) {
    // initalize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // ceate SSLContext
    ctx_.reset(SSL_CTX_new(TLS_server_method()));
    if (!ctx_) {
        LOG_ERROR("SSL_CTX_new failed");
        return false;
    }

    // config
    SSL_CTX_set_options(ctx_.get(), SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_mode(ctx_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);

    // load cert and key
    if (SSL_CTX_use_certificate_file(ctx_.get(), certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("SSL_CTX_use_certificate_file failed: %s", certPath.c_str());
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_.get(), keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("SSL_CTX_use_PrivateKey_file failed: %s", keyPath.c_str());
        return false;
    }

    if (!SSL_CTX_check_private_key(ctx_.get())) {
        LOG_ERROR("Private key does not match the public certificate");
        return false;
    }

    // load CA(optional for client cert verify)
    if (!caPath.empty()) {
        if (SSL_CTX_load_verify_locations(ctx_.get(), caPath.c_str(), nullptr) <= 0) {
            LOG_ERROR("SSL_CTX_load_verify_locations failed");
            return false;
        }
        SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    }

    initialized_ = true;
    LOG_INFO("SSLContext initialized successfully");
    return true;
}

