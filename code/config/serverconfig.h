#pragma once

#include <string>

enum class TriggerMode { LT, ET };

struct ServerOptions {
    int port = 8888;
    TriggerMode listenMode = TriggerMode::ET;
    TriggerMode connectionMode = TriggerMode::ET;

    int idleTimeoutMs = 60000;
    bool linger = false;
    int workerThreads = 6;
    std::string documentRoot = "./rss";
};

struct DatabaseConfig {
    std::string host = "localhost";
    int port = 3306;

    std::string user;
    std::string password;
    std::string name;
    int poolSize = 12;
};

struct TlsConfig {
    bool enabled = false;
    std::string certificate;
    std::string privateKey;
    //客户端证书的信任源,是否要求证书由独立开关控制
    std::string clientCaFile;
    bool requireClientCertificate = false;
};

struct LoggingConfig {
    bool enabled = true;
    int level = 1;
    int asyncQueueSize = 1024;
};

struct ServerConfig {
    ServerOptions server;
    DatabaseConfig database;
    TlsConfig tls;
    LoggingConfig logging;
};
