#include "../sslContext.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <vector>

using namespace std;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

struct Fixture {
    filesystem::path root;

    Fixture() {
        char pattern[] = "/tmp/tinyweb-tls-context-XXXXXX";
        const char* directory = mkdtemp(pattern);
        Require(directory != nullptr, "mkdtemp failed");
        root = directory;
    }

    ~Fixture() {
        error_code ignored;
        filesystem::remove_all(root, ignored);
    }

    void CreateCertificate(const string& name) const {
        unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> generator(
            EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), &EVP_PKEY_CTX_free);
        Require(generator && EVP_PKEY_keygen_init(generator.get()) == 1 &&
                EVP_PKEY_CTX_set_rsa_keygen_bits(generator.get(), 2048) == 1, "key generation setup failed");
        EVP_PKEY* rawKey = nullptr;
        Require(EVP_PKEY_keygen(generator.get(), &rawKey) == 1, "key generation failed");
        unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, &EVP_PKEY_free);
        unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), &X509_free);
        Require(certificate != nullptr, "X509_new failed");
        X509_set_version(certificate.get(), 2);
        ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
        X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60);
        X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600);
        Require(X509_set_pubkey(certificate.get(), key.get()) == 1, "cannot assign certificate key");
        auto* subject = X509_get_subject_name(certificate.get());
        Require(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(name.c_str()), -1, -1, 0) == 1,
            "cannot assign certificate name");
        X509_set_issuer_name(certificate.get(), subject);
        unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> constraint(
            X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "critical,CA:TRUE"),
            &X509_EXTENSION_free);
        Require(constraint && X509_add_ext(certificate.get(), constraint.get(), -1) == 1,
                "cannot assign CA constraint");
        Require(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0, "certificate signing failed");
        unique_ptr<BIO, decltype(&BIO_free)> certificateFile(
            BIO_new_file((root / (name + ".crt")).c_str(), "w"), &BIO_free);
        unique_ptr<BIO, decltype(&BIO_free)> keyFile(
            BIO_new_file((root / (name + ".key")).c_str(), "w"), &BIO_free);
        Require(certificateFile && keyFile &&
            PEM_write_bio_X509(certificateFile.get(), certificate.get()) == 1 &&
            PEM_write_bio_PrivateKey(keyFile.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1,
            "cannot write certificate fixtures");
    }
};

bool Handshake(SSL_CTX* serverContext, const Fixture& fixture, const string& clientIdentity) {
    unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> clientContext(SSL_CTX_new(TLS_client_method()), &SSL_CTX_free);
    Require(clientContext && SSL_CTX_load_verify_locations(clientContext.get(),
            (fixture.root / "server.crt").c_str(), nullptr) == 1, "cannot trust test server");
    SSL_CTX_set_verify(clientContext.get(), SSL_VERIFY_PEER, nullptr);
    if (!clientIdentity.empty()) {
        Require(SSL_CTX_use_certificate_file(clientContext.get(),
            (fixture.root / (clientIdentity + ".crt")).c_str(), SSL_FILETYPE_PEM) == 1 &&
            SSL_CTX_use_PrivateKey_file(clientContext.get(),
            (fixture.root / (clientIdentity + ".key")).c_str(), SSL_FILETYPE_PEM) == 1,
            "cannot load test client identity");
    }
    unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(serverContext), &SSL_free);
    unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(clientContext.get()), &SSL_free);
    Require(server && client, "SSL_new failed");
    BIO* serverBio = nullptr;
    BIO* clientBio = nullptr;
    Require(BIO_new_bio_pair(&serverBio, 0, &clientBio, 0) == 1, "BIO pair failed");
    SSL_set_bio(server.get(), serverBio, serverBio);
    SSL_set_bio(client.get(), clientBio, clientBio);
    SSL_set_accept_state(server.get());
    SSL_set_connect_state(client.get());
    bool serverDone = false;
    bool clientDone = false;
    const auto advance = [](SSL* ssl, bool& done) {
        if (done) {
            return true;
        }
        const int result = SSL_do_handshake(ssl);
        if (result == 1) {
            done = true;
            return true;
        }
        const int error = SSL_get_error(ssl, result);
        return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
    };
    for (int i = 0; i < 1000; ++i) {
        if (!advance(client.get(), clientDone) || !advance(server.get(), serverDone)) {
            return false;
        }
        if (serverDone && clientDone) {
            return true;
        }
    }
    throw runtime_error("test handshake made no progress");
}
}

int main(int argc, char* argv[]) {
    const vector<string> cases = {"single_tls", "ca_without_mtls", "required_missing", "required_valid",
        "untrusted_client", "missing_ca", "bad_cert", "bad_key", "mismatched_key", "retry_after_failure"};
    if (argc != 2) {
        return 2;
    }
    const string name(argv[1]);
    if (name == "--list") {
        for (const auto& item : cases) {
            cout << item << '\n';
        }
        return 0;
    }
    try {
        Require(find(cases.begin(), cases.end(), name) != cases.end(), "unknown test case");
        Fixture fixture;
        fixture.CreateCertificate("server");
        const string certificate = (fixture.root / "server.crt").string();
        const string key = (fixture.root / "server.key").string();
        auto& context = SSLContext::GetInstance();
        if (name == "missing_ca" || name == "bad_cert" || name == "bad_key" ||
            name == "mismatched_key" || name == "retry_after_failure") {
            if (name == "mismatched_key") {
                fixture.CreateCertificate("other");
            }
            const string selectedCertificate = name == "bad_cert" || name == "retry_after_failure"
                ? (fixture.root / "missing.crt").string() : certificate;
            const string selectedKey = name == "bad_key" ? (fixture.root / "missing.key").string()
                : name == "mismatched_key" ? (fixture.root / "other.key").string() : key;
            Require(!context.Initialize(selectedCertificate, selectedKey, "", name == "missing_ca"),
                    "invalid TLS context was accepted");
            Require(context.GetContext() == nullptr && !context.GetLastError().empty(),
                    "failed TLS initialization published a context or lost its error");
            if (name == "retry_after_failure") {
                Require(context.Initialize(certificate, key) && Handshake(context.GetContext(), fixture, ""),
                        "TLS initialization could not recover after failure");
            }
        } else {
            const bool required = name == "required_missing" || name == "required_valid" || name == "untrusted_client";
            Require(context.Initialize(certificate, key, name == "single_tls" ? "" : certificate, required),
                    "TLS initialization failed");
            const long expectedMode = required ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE;
            Require(SSL_CTX_get_verify_mode(context.GetContext()) == expectedMode, "wrong TLS verification mode");
            string identity;
            if (name == "required_valid") {
                identity = "server";
            } else if (name == "untrusted_client") {
                fixture.CreateCertificate("other");
                identity = "other";
            }
            const bool expectedSuccess = name != "required_missing" && name != "untrusted_client";
            Require(Handshake(context.GetContext(), fixture, identity) == expectedSuccess,
                    "TLS handshake did not enforce the configured client verification policy");
        }
        cout << "PASS " << name << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "FAIL " << name << ": " << error.what() << '\n';
        return 1;
    }
}
