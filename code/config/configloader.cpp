#include "configloader.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

using namespace std;

namespace {
//Fail通过抛出异常结束,不会沿正常调用路径返回
[[noreturn]] void Fail(string_view field, string_view reason) {
    throw runtime_error(string(field) + ": " + string(reason));
}

//使用initializer_list,避免额外构造一次vector
void CheckKeys(const toml::table& table, string_view section, initializer_list<string_view> allowed) {
    for (const auto& entry : table) {
        const string_view key = entry.first.str();
        if (find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            const string field = section.empty() ? string(key) : string(section) + "." + string(key);
            Fail(field, "unknown field");
        }
    }
}

const toml::table& Section(const toml::table& root, string_view name) {
    const auto* node = root.get(name);
    if (!node) {
        static const toml::table empty;
        return empty;
    }
    const auto* table = node->as_table();
    if (!table) {
        Fail(name, "expected a table");
    }
    return *table;
}

template <typename T>
T ReadOptional(const toml::table& table, string_view key, const T& fallback, string_view field) {
    const auto* node = table.get(key);
    if (!node) {
        return fallback;
    }
    const auto value = node->value_exact<T>();
    if (!value) {
        Fail(field, "wrong type or value outside the target type's range");
    }
    return *value;
}

//整数使用ReadInteger,避免value_exact<int>()的类型限制
int ReadInteger(const toml::table& table, string_view key, int fallback, string_view field) {
    const auto* node = table.get(key);
    if (!node) {
        return fallback;
    }
    const auto value = node->value_exact<int64_t>();
    if (!value) {
        Fail(field, "expected an integer");
    }
    if (*value < numeric_limits<int>::min() || *value > numeric_limits<int>::max()) {
        Fail(field, "integer outside the range of int");
    }
    return static_cast<int>(*value);
}

void CheckText(const string& text, string_view field) {
    if (text.find('\0') != string::npos) {
        Fail(field, "NUL characters are not allowed");
    }
}

string ReadDatabasePassword(const toml::table& table, const string& fallback) {
    const bool hasPassword = table.get("password") != nullptr;
    const bool hasPasswordEnv = table.get("passwordEnv") != nullptr;
    if (hasPassword && hasPasswordEnv) {
        Fail("database.passwordEnv", "cannot be used together with database.password");
    }
    if (!hasPasswordEnv) {
        return ReadOptional(table, "password", fallback, "database.password");
    }

    const auto variable = ReadOptional(table, "passwordEnv", string{}, "database.passwordEnv");
    CheckText(variable, "database.passwordEnv");
    if (variable.empty() || variable.find('=') != string::npos) {
        Fail("database.passwordEnv", "must name a nonempty environment variable without '='");
    }

    const char* value = std::getenv(variable.c_str());
    if (value == nullptr) {
        Fail("database.passwordEnv", "referenced environment variable is not set");
    }
    if (value[0] == '\0') {
        Fail("database.passwordEnv", "referenced environment variable must not be empty");
    }
    // 复制到配置对象，不保留 getenv() 返回的指针，也不输出密码值。
    return string(value);
}

TriggerMode ReadMode(const toml::table& table, string_view key, TriggerMode fallback, string_view field) {
    const auto text = ReadOptional(table, key, string(fallback == TriggerMode::ET ? "et" : "lt"), field);
    if (text == "et") {
        return TriggerMode::ET;
    }
    if (text == "lt") {
        return TriggerMode::LT;
    }
    Fail(field, "expected 'et' or 'lt'");
}

void ResolvePath(string& text, const filesystem::path& base, string_view field) {
    CheckText(text, field);
    if (text.empty()) {
        return;
    }
    const filesystem::path path(text);
    error_code error;
    //不要求路径存在,整理路径结果
    const auto resolved = filesystem::weakly_canonical(path.is_absolute() ? path : base / path, error);
    if (error) {
        Fail(field, "cannot resolve path: " + error.message());
    }
    text = resolved.string();
}

void RequireDirectory(const string& path, string_view field) {
    error_code error;
    if (path.empty() || !filesystem::is_directory(path, error)) {
        Fail(field, "must name an existing directory");
    }
}

void RequireRegularFile(const string& path, string_view field) {
    error_code error;
    if (path.empty() || !filesystem::is_regular_file(path, error)) {
        Fail(field, "must name an existing regular file");
    }
}

void Validate(const ServerConfig& config) {
    const auto& server = config.server;
    if (server.port < 1024 || server.port > 65535) {
        Fail("server.port", "must be between 1024 and 65535");
    }
    if (server.idleTimeoutMs < 0) {
        Fail("server.idleTimeoutMs", "must be nonnegative; 0 disables the idle timer");
    }
    if (server.workerThreads <= 0) {
        Fail("server.workerThreads", "must be positive");
    }
    RequireDirectory(server.documentRoot, "server.documentRoot");

    const auto& db = config.database;
    CheckText(db.host, "database.host");
    CheckText(db.user, "database.user");
    CheckText(db.password, "database.password");
    CheckText(db.name, "database.name");
    if (db.host.empty()) {
        Fail("database.host", "must not be empty");
    }
    if (db.user.empty()) {
        Fail("database.user", "must not be empty");
    }
    if (db.name.empty()) {
        Fail("database.name", "must not be empty");
    }
    if (db.port < 1 || db.port > 65535) {
        Fail("database.port", "must be between 1 and 65535");
    }
    if (db.poolSize <= 0) {
        Fail("database.poolSize", "must be positive");
    }

    const auto& logging = config.logging;
    if (logging.level < 0 || logging.level > 3) {
        Fail("logging.level", "must be between 0 and 3");
    }
    if (logging.asyncQueueSize < 0) {
        Fail("logging.asyncQueueSize", "must be nonnegative");
    }

    if (config.plugin.enabled) {
        RequireRegularFile(config.plugin.path, "plugin.path");
    }

    const auto& tls = config.tls;
    if (tls.enabled) {
        if (tls.requireClientCertificate && tls.clientCaFile.empty()) {
            Fail("tls.clientCaFile", "must be set when tls.requireClientCertificate is true");
        }
        RequireRegularFile(tls.certificate, "tls.certificate");
        RequireRegularFile(tls.privateKey, "tls.privateKey");
        if (!tls.clientCaFile.empty()) {
            RequireRegularFile(tls.clientCaFile, "tls.clientCaFile");
        }
    }
}

} // namespace

ServerConfig LoadConfig(const filesystem::path& path) {
    if (path.empty()) {
        Fail("config", "file path must not be empty");
    }
    CheckText(path.string(), "config");
    const auto configFile = filesystem::absolute(path);
    try {
        const toml::table root = toml::parse_file(configFile.string());
        CheckKeys(root, "", {"server", "database", "tls", "logging", "plugin"});

        const auto& server = Section(root, "server");
        const auto& db = Section(root, "database");
        const auto& tls = Section(root, "tls");
        const auto& logging = Section(root, "logging");
        const auto& plugin = Section(root, "plugin");
        CheckKeys(server, "server", {"port", "listenMode", "connectionMode", "idleTimeoutMs",
                                    "linger", "workerThreads", "documentRoot"});
        CheckKeys(db, "database", {"host", "port", "user", "password", "passwordEnv", "name", "poolSize"});
        CheckKeys(tls, "tls", {"enabled", "certificate", "privateKey", "clientCaFile",
                               "requireClientCertificate"});
        CheckKeys(logging, "logging", {"enabled", "level", "asyncQueueSize"});
        CheckKeys(plugin, "plugin", {"enabled", "path"});

        ServerConfig config;
        config.server.port = ReadInteger(server, "port", config.server.port, "server.port");
        config.server.listenMode = ReadMode(server, "listenMode", config.server.listenMode, "server.listenMode");
        config.server.connectionMode = ReadMode(server, "connectionMode", config.server.connectionMode, "server.connectionMode");
        config.server.idleTimeoutMs = ReadInteger(server, "idleTimeoutMs", config.server.idleTimeoutMs,
                                                 "server.idleTimeoutMs");
        config.server.linger = ReadOptional(server, "linger", config.server.linger, "server.linger");
        config.server.workerThreads = ReadInteger(server, "workerThreads", config.server.workerThreads,
                                                 "server.workerThreads");
        config.server.documentRoot = ReadOptional(server, "documentRoot", config.server.documentRoot,
                                                  "server.documentRoot");

        config.database.host = ReadOptional(db, "host", config.database.host, "database.host");
        config.database.port = ReadInteger(db, "port", config.database.port, "database.port");
        config.database.user = ReadOptional(db, "user", config.database.user, "database.user");
        config.database.password = ReadDatabasePassword(db, config.database.password);
        config.database.name = ReadOptional(db, "name", config.database.name, "database.name");
        config.database.poolSize = ReadInteger(db, "poolSize", config.database.poolSize, "database.poolSize");

        config.tls.enabled = ReadOptional(tls, "enabled", config.tls.enabled, "tls.enabled");
        config.tls.certificate = ReadOptional(tls, "certificate", config.tls.certificate, "tls.certificate");
        config.tls.privateKey = ReadOptional(tls, "privateKey", config.tls.privateKey, "tls.privateKey");
        config.tls.clientCaFile = ReadOptional(tls, "clientCaFile", config.tls.clientCaFile, "tls.clientCaFile");
        config.tls.requireClientCertificate = ReadOptional(tls, "requireClientCertificate",
            config.tls.requireClientCertificate, "tls.requireClientCertificate");

        config.logging.enabled = ReadOptional(logging, "enabled", config.logging.enabled, "logging.enabled");
        config.logging.level = ReadInteger(logging, "level", config.logging.level, "logging.level");
        config.logging.asyncQueueSize = ReadInteger(logging, "asyncQueueSize", config.logging.asyncQueueSize,
                                                   "logging.asyncQueueSize");

        config.plugin.enabled = ReadOptional(plugin, "enabled", config.plugin.enabled, "plugin.enabled");
        config.plugin.path = ReadOptional(plugin, "path", config.plugin.path, "plugin.path");

        const auto base = configFile.parent_path();
        ResolvePath(config.server.documentRoot, base, "server.documentRoot");
        ResolvePath(config.tls.certificate, base, "tls.certificate");
        ResolvePath(config.tls.privateKey, base, "tls.privateKey");
        ResolvePath(config.tls.clientCaFile, base, "tls.clientCaFile");
        ResolvePath(config.plugin.path, base, "plugin.path");
        Validate(config);
        return config;
    } catch (const toml::parse_error& error) {
        ostringstream message;
        message << configFile.string() << ": TOML parse error at "
                << error.source().begin.line << ':' << error.source().begin.column
                << ": " << error.description();
        throw runtime_error(message.str());
    } catch (const exception& error) {
        throw runtime_error(configFile.string() + ": " + error.what());
    }
}
