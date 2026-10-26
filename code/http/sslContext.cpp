#include "sslContext.h"

#include <cstring>
#include <utility>

#include "../log/log.h"

using namespace std;

SSLContext& SSLContext::GetInstance() {
    static SSLContext instance;
    return instance;
}

bool SSLContext::Initialize(const char* certPath,
    const char* keyPath, const char* caPath, bool requireClientCertificate) {
    ERR_clear_error();
    lastError_.clear();
    const auto fail = [this](const char* operation) {
        lastError_ = operation;
        const unsigned long error = ERR_get_error();
        if (error != 0) {
            char description[256]{};
            ERR_error_string_n(error, description, sizeof(description));
            lastError_ += ": ";
            lastError_ += description;
        }
        LOG_ERROR("%s", lastError_.c_str());
        return false;
    };
    if (initialized_) {
        return fail("TLS context is already initialized");
    }
    if (certPath == nullptr || certPath[0] == '\0' ||
        keyPath == nullptr || keyPath[0] == '\0') {
        return fail("TLS certificate and private key must be set");
    }
    if (requireClientCertificate && (caPath == nullptr || caPath[0] == '\0')) {
        return fail("Client certificate verification requires a client CA file");
    }

    //先构造候选上下文,只有全部校验成功后才发布
    unique_ptr<SSL_CTX, SSLCTXDeleter> candidate(SSL_CTX_new(TLS_server_method()));
    if (!candidate) {
        return fail("SSL_CTX_new failed");
    }
    SSL_CTX_set_options(candidate.get(), SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_mode(candidate.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);

    if (SSL_CTX_use_certificate_file(candidate.get(), certPath, SSL_FILETYPE_PEM) <= 0) {
        return fail("Cannot load the TLS certificate");
    }
    if (SSL_CTX_use_PrivateKey_file(candidate.get(), keyPath, SSL_FILETYPE_PEM) <= 0) {
        return fail("Cannot load the TLS private key");
    }
    if (SSL_CTX_check_private_key(candidate.get()) != 1) {
        return fail("TLS private key does not match the certificate");
    }
    if (caPath != nullptr && caPath[0] != '\0') {
        if (SSL_CTX_load_verify_locations(candidate.get(), caPath, nullptr) != 1) {
            return fail("Cannot load the client CA file");
        }
    }

    //CA文件只提供信任源,独立开关决定是否要求客户端证书
    const int verifyMode = requireClientCertificate
        ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
        : SSL_VERIFY_NONE;
    SSL_CTX_set_verify(candidate.get(), verifyMode, nullptr);
    ctx_ = move(candidate);
    initialized_ = true;
    LOG_INFO("SSLContext initialized successfully");
    return true;
}

bool SSLContext::Initialize(const string& certPath,
    const string& keyPath, const string& caPath, bool requireClientCertificate) {
    return Initialize(certPath.c_str(), keyPath.c_str(),
        caPath.empty() ? nullptr : caPath.c_str(), requireClientCertificate);
}
