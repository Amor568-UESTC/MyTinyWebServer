#include "../configloader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::string validConfig = R"(
[server]
port = 9443
listenMode = "lt"
connectionMode = "et"
idleTimeoutMs = 12345
linger = true
workerThreads = 3
documentRoot = "../site"

[database]
host = "127.0.0.1"
port = 3307
user = "test-user"
password = "test-password"
name = "testdb"
poolSize = 4

[tls]
enabled = true
certificate = "../tls/server.crt"
privateKey = "../tls/server.key"
clientCaFile = ""
requireClientCertificate = false

[logging]
enabled = true
level = 2
asyncQueueSize = 16
)";

void Require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string Replace(std::string text, std::string_view from, std::string_view to) {
    const auto position = text.find(from);
    Require(position != std::string::npos, "test replacement was not found");
    text.replace(position, from.size(), to);
    return text;
}

class Fixture {
public:
    std::filesystem::path root;

    Fixture() {
        auto pattern = (std::filesystem::temp_directory_path() / "tinyweb-config-test-XXXXXX").string();
        const auto* directory = ::mkdtemp(pattern.data());
        Require(directory != nullptr, "cannot create temporary directory");
        root = directory;
        std::filesystem::create_directories(root / "config/rss");
        std::filesystem::create_directories(root / "site");
        std::filesystem::create_directories(root / "tls");
        std::filesystem::create_directories(root / "plugins");
        //这里只验证文件类型,动态库内容由Module负责校验
        std::ofstream(root / "plugins/business.so") << "plugin path fixture";
        // The loader checks file types only; these are not real certificates or keys.
        std::ofstream(root / "tls/server.crt") << "certificate fixture";
        std::ofstream(root / "tls/server.key") << "key fixture";
        std::ofstream(root / "tls/ca.crt") << "CA fixture";
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path Write(std::string_view text) const {
        const auto file = root / "config/server.toml";
        std::ofstream output(file);
        Require(output.is_open(), "cannot write fixture config");
        output << text;
        Require(output.good(), "cannot write fixture config");
        return file;
    }
};

class ScopedDirectory {
    std::filesystem::path previous_;

public:
    explicit ScopedDirectory(const std::filesystem::path& directory)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(directory);
    }

    ~ScopedDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }
};

class ScopedEnvironment {
    std::string name_;
    std::optional<std::string> previous_;

public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        const char* previous = std::getenv(name_.c_str());
        if (previous != nullptr) {
            previous_ = previous;
        }
        const int result = value == nullptr
            ? ::unsetenv(name_.c_str())
            : ::setenv(name_.c_str(), value, 1);
        Require(result == 0, "cannot prepare test environment");
    }

    ~ScopedEnvironment() {
        if (previous_.has_value()) {
            ::setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }
};

class Runner {
    int passed_ = 0;
    int failed_ = 0;

public:
    template <typename Function>
    void Run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }

    int Finish() const {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }
};

struct InvalidCase {
    const char* name;
    std::string text;
    const char* field;
};

} // namespace

int main() {
    try {
        Fixture fixture;
        Runner runner;
        ScopedEnvironment password("TINYWEB_TEST_DB_PASSWORD", "test-env-password");
        ScopedEnvironment missingPassword("TINYWEB_TEST_DB_PASSWORD_MISSING", nullptr);
        ScopedEnvironment emptyPassword("TINYWEB_TEST_DB_PASSWORD_EMPTY", "");
        const auto envConfig = Replace(validConfig, "password = \"test-password\"",
                                       "passwordEnv = \"TINYWEB_TEST_DB_PASSWORD\"");

        runner.Run("all fields and owned strings", [&] {
            const auto config = LoadConfig(fixture.Write(validConfig));
            Require(config.server.port == 9443, "port");
            Require(config.server.listenMode == TriggerMode::LT, "listenMode");
            Require(config.server.connectionMode == TriggerMode::ET, "connectionMode");
            Require(config.server.idleTimeoutMs == 12345, "idleTimeoutMs");
            Require(config.server.linger && config.server.workerThreads == 3, "linger or workerThreads");
            Require(config.server.documentRoot == (fixture.root / "site").string(), "documentRoot");
            Require(config.database.host == "127.0.0.1" && config.database.port == 3307, "database address");
            Require(config.database.user == "test-user" && config.database.password == "test-password", "credentials");
            Require(config.database.name == "testdb" && config.database.poolSize == 4, "database name or poolSize");
            Require(config.tls.enabled, "TLS enabled");
            Require(config.tls.certificate == (fixture.root / "tls/server.crt").string(), "certificate path");
            Require(config.tls.privateKey == (fixture.root / "tls/server.key").string(), "key path");
            Require(config.tls.clientCaFile.empty(), "default client CA");
            Require(!config.tls.requireClientCertificate, "client certificate default");
            Require(config.logging.enabled && config.logging.level == 2, "logging enabled or level");
            Require(config.logging.asyncQueueSize == 16, "asyncQueueSize");
            Require(!config.plugin.enabled && config.plugin.path.empty(), "plugin defaults");
        });

        runner.Run("missing optional fields use defaults", [&] {
            const auto config = LoadConfig(fixture.Write(R"(
[server]
documentRoot = "../site"
[database]
user = "test-user"
name = "testdb"
)"));
            Require(config.server.port == 8888 && config.server.workerThreads == 6, "server defaults");
            Require(config.server.idleTimeoutMs == 60000 && !config.server.linger, "connection defaults");
            Require(config.server.listenMode == TriggerMode::ET &&
                    config.server.connectionMode == TriggerMode::ET, "mode defaults");
            Require(config.database.host == "localhost" && config.database.port == 3306 &&
                    config.database.poolSize == 12, "database defaults");
            Require(config.database.password.empty() && !config.tls.enabled, "password or TLS defaults");
            Require(!config.tls.requireClientCertificate, "client certificate default");
            Require(config.logging.enabled && config.logging.level == 1 &&
                    config.logging.asyncQueueSize == 1024, "logging defaults");
        });

        runner.Run("environment password is copied and read again for a new config", [&] {
            const auto config = LoadConfig(fixture.Write(envConfig));
            Require(config.database.password == "test-env-password", "environment password");
            ScopedEnvironment rotated("TINYWEB_TEST_DB_PASSWORD", "rotated-test-password");
            Require(config.database.password == "test-env-password", "loaded config borrowed environment storage");
            Require(LoadConfig(fixture.Write(envConfig)).database.password == "rotated-test-password",
                    "new config did not read the current environment");
        });

        runner.Run("environment password preserves special characters", [&] {
            const std::string secret = "spaces = 'quoted' \"double\" \\ $value\nnext line";
            ScopedEnvironment special("TINYWEB_TEST_DB_PASSWORD", secret.c_str());
            Require(LoadConfig(fixture.Write(envConfig)).database.password == secret,
                    "environment password was modified");
        });

        runner.Run("literal password is not an environment reference", [&] {
            const auto text = Replace(validConfig, "password = \"test-password\"",
                                      "password = \"TINYWEB_TEST_DB_PASSWORD\"");
            Require(LoadConfig(fixture.Write(text)).database.password == "TINYWEB_TEST_DB_PASSWORD",
                    "literal password was interpreted as an environment variable");
        });

        runner.Run("explicit empty literal password remains supported", [&] {
            const auto text = Replace(validConfig, "password = \"test-password\"", "password = \"\"");
            Require(LoadConfig(fixture.Write(text)).database.password.empty(), "empty literal password");
        });

        runner.Run("absent server section resolves default path relative to file", [&] {
            const auto config = LoadConfig(fixture.Write("[database]\nuser = 'test-user'\nname = 'testdb'\n"));
            Require(config.server.documentRoot == (fixture.root / "config/rss").string(), "default path base");
        });

        runner.Run("zero idle timeout and synchronous logging", [&] {
            auto text = Replace(validConfig, "idleTimeoutMs = 12345", "idleTimeoutMs = 0");
            text = Replace(text, "asyncQueueSize = 16", "asyncQueueSize = 0");
            const auto config = LoadConfig(fixture.Write(text));
            Require(config.server.idleTimeoutMs == 0 && config.logging.asyncQueueSize == 0, "zero values");
        });

        runner.Run("disabled TLS permits empty certificate paths", [&] {
            auto text = Replace(validConfig, "[tls]\nenabled = true", "[tls]\nenabled = false");
            text = Replace(text, "certificate = \"../tls/server.crt\"", "certificate = \"\"");
            text = Replace(text, "privateKey = \"../tls/server.key\"", "privateKey = \"\"");
            const auto config = LoadConfig(fixture.Write(text));
            Require(!config.tls.enabled && config.tls.certificate.empty() && config.tls.privateKey.empty(), "disabled TLS");
        });

        runner.Run("optional client CA path", [&] {
            const auto text = Replace(validConfig, "clientCaFile = \"\"", "clientCaFile = \"../tls/ca.crt\"");
            const auto config = LoadConfig(fixture.Write(text));
            Require(config.tls.clientCaFile == (fixture.root / "tls/ca.crt").string(), "client CA path");
            Require(!config.tls.requireClientCertificate, "client CA unexpectedly enabled mutual TLS");
        });

        runner.Run("required client certificate with a CA", [&] {
            auto text = Replace(validConfig, "requireClientCertificate = false", "requireClientCertificate = true");
            text = Replace(text, "clientCaFile = \"\"", "clientCaFile = \"../tls/ca.crt\"");
            Require(LoadConfig(fixture.Write(text)).tls.requireClientCertificate, "mutual TLS setting");
        });

        runner.Run("disabled TLS permits an unused client verification setting", [&] {
            auto text = Replace(validConfig, "[tls]\nenabled = true", "[tls]\nenabled = false");
            text = Replace(text, "requireClientCertificate = false", "requireClientCertificate = true");
            Require(!LoadConfig(fixture.Write(text)).tls.enabled, "disabled TLS");
        });

        runner.Run("absolute resource path", [&] {
            const auto text = Replace(validConfig, "documentRoot = \"../site\"",
                                      "documentRoot = \"" + (fixture.root / "site").string() + "\"");
            Require(LoadConfig(fixture.Write(text)).server.documentRoot == (fixture.root / "site").string(), "absolute path");
        });

        runner.Run("config paths work from a different working directory", [&] {
            const auto file = fixture.Write(validConfig);
            ScopedDirectory directory(fixture.root);
            const auto relative = LoadConfig("config/server.toml");
            const auto absolute = LoadConfig(file);
            Require(relative.server.documentRoot == absolute.server.documentRoot, "resource path changed with cwd");
            Require(relative.tls.privateKey == absolute.tls.privateKey, "TLS path changed with cwd");
        });

        const auto pluginConfig = validConfig + "\n[plugin]\nenabled = true\npath = '../plugins/business.so'\n";
        runner.Run("plugin path is resolved from the config directory", [&] {
            const auto config = LoadConfig(fixture.Write(pluginConfig));
            Require(config.plugin.enabled && config.plugin.path == (fixture.root / "plugins/business.so").string(),
                    "plugin path or enabled flag");
        });
        runner.Run("absolute plugin path", [&] {
            const auto text = Replace(pluginConfig, "../plugins/business.so",
                                      (fixture.root / "plugins/business.so").string());
            Require(LoadConfig(fixture.Write(text)).plugin.path == (fixture.root / "plugins/business.so").string(),
                    "absolute plugin path");
        });
        runner.Run("plugin path works from another working directory", [&] {
            const auto file = fixture.Write(pluginConfig);
            ScopedDirectory directory(fixture.root);
            Require(LoadConfig(file).plugin.path == (fixture.root / "plugins/business.so").string(),
                    "plugin path changed with cwd");
        });
        runner.Run("disabled plugin permits a missing file", [&] {
            const auto text = validConfig + "\n[plugin]\nenabled = false\npath = '../missing.so'\n";
            const auto config = LoadConfig(fixture.Write(text));
            Require(!config.plugin.enabled && config.plugin.path == (fixture.root / "missing.so").string(),
                    "disabled plugin path");
        });

        const std::vector<InvalidCase> cases = {
            {"plugin section type", "plugin = 3\n" + validConfig, "plugin"},
            {"plugin enabled type", validConfig + "\n[plugin]\nenabled = 'true'\n", "plugin.enabled"},
            {"plugin path type", validConfig + "\n[plugin]\npath = 123\n", "plugin.path"},
            {"plugin unknown field", validConfig + "\n[plugin]\nunknown = true\n", "plugin.unknown"},
            {"enabled plugin without path", validConfig + "\n[plugin]\nenabled = true\n", "plugin.path"},
            {"plugin file missing", Replace(pluginConfig, "business.so", "missing.so"), "plugin.path"},
            {"plugin path is directory", Replace(pluginConfig, "../plugins/business.so", "../plugins"), "plugin.path"},
            {"NUL plugin path", validConfig + "\n[plugin]\npath = \"bad\\u0000path\"\n", "plugin.path"},
            {"integer string", Replace(validConfig, "port = 9443", "port = \"9443\""), "server.port"},
            {"integer float", Replace(validConfig, "port = 9443", "port = 9443.0"), "server.port"},
            {"integer boolean", Replace(validConfig, "port = 9443", "port = true"), "server.port"},
            {"integer narrowing overflow", Replace(validConfig, "port = 9443", "port = 2147483648"), "server.port"},
            {"port range", Replace(validConfig, "port = 9443", "port = 80"), "server.port"},
            {"negative timeout", Replace(validConfig, "idleTimeoutMs = 12345", "idleTimeoutMs = -1"), "server.idleTimeoutMs"},
            {"zero threads", Replace(validConfig, "workerThreads = 3", "workerThreads = 0"), "server.workerThreads"},
            {"boolean type", Replace(validConfig, "linger = true", "linger = \"true\""), "server.linger"},
            {"unknown root key", "unknown = true\n" + validConfig, "unknown"},
            {"unknown field", Replace(validConfig, "port = 9443", "port = 9443\nporrt = 1234"), "server.porrt"},
            {"old underscore key", Replace(validConfig, "workerThreads = 3", "worker_threads = 3"), "server.worker_threads"},
            {"section type", "server = 3\n", "server"},
            {"mode value", Replace(validConfig, "listenMode = \"lt\"", "listenMode = \"wrong\""), "server.listenMode"},
            {"mode type", Replace(validConfig, "connectionMode = \"et\"", "connectionMode = 3"), "server.connectionMode"},
            {"database string type", Replace(validConfig, "host = \"127.0.0.1\"", "host = 12"), "database.host"},
            {"password type", Replace(validConfig, "password = \"test-password\"", "password = 42"), "database.password"},
            {"environment name type", Replace(envConfig, "passwordEnv = \"TINYWEB_TEST_DB_PASSWORD\"", "passwordEnv = 42"), "database.passwordEnv"},
            {"empty environment name", Replace(envConfig, "passwordEnv = \"TINYWEB_TEST_DB_PASSWORD\"", "passwordEnv = \"\""), "database.passwordEnv"},
            {"NUL environment name", Replace(envConfig, "passwordEnv = \"TINYWEB_TEST_DB_PASSWORD\"", R"(passwordEnv = "TINYWEB_TEST_DB_PASSWORD\u0000TAIL")"), "database.passwordEnv"},
            {"equals in environment name", Replace(envConfig, "passwordEnv = \"TINYWEB_TEST_DB_PASSWORD\"", "passwordEnv = \"TINYWEB_TEST=PASSWORD\""), "database.passwordEnv"},
            {"missing environment variable", Replace(envConfig, "TINYWEB_TEST_DB_PASSWORD", "TINYWEB_TEST_DB_PASSWORD_MISSING"), "database.passwordEnv"},
            {"empty environment value", Replace(envConfig, "TINYWEB_TEST_DB_PASSWORD", "TINYWEB_TEST_DB_PASSWORD_EMPTY"), "database.passwordEnv"},
            {"conflicting password sources", Replace(validConfig, "password = \"test-password\"", "password = \"test-password\"\npasswordEnv = \"TINYWEB_TEST_DB_PASSWORD\""), "database.passwordEnv"},
            {"empty literal still conflicts with environment", Replace(validConfig, "password = \"test-password\"", "password = \"\"\npasswordEnv = \"TINYWEB_TEST_DB_PASSWORD\""), "database.passwordEnv"},
            {"empty database user", Replace(validConfig, "user = \"test-user\"", "user = \"\""), "database.user"},
            {"empty database name", Replace(validConfig, "name = \"testdb\"", "name = \"\""), "database.name"},
            {"database port range", Replace(validConfig, "port = 3307", "port = 0"), "database.port"},
            {"database pool range", Replace(validConfig, "poolSize = 4", "poolSize = 0"), "database.poolSize"},
            {"resource directory missing", Replace(validConfig, "documentRoot = \"../site\"", "documentRoot = \"../missing\""), "server.documentRoot"},
            {"resource path is file", Replace(validConfig, "documentRoot = \"../site\"", "documentRoot = \"../tls/server.crt\""), "server.documentRoot"},
            {"TLS boolean type", Replace(validConfig, "[tls]\nenabled = true", "[tls]\nenabled = \"true\""), "tls.enabled"},
            {"TLS certificate missing", Replace(validConfig, "server.crt", "missing.crt"), "tls.certificate"},
            {"TLS key missing", Replace(validConfig, "server.key", "missing.key"), "tls.privateKey"},
            {"TLS certificate is directory", Replace(validConfig, "certificate = \"../tls/server.crt\"", "certificate = \"../site\""), "tls.certificate"},
            {"client CA missing", Replace(validConfig, "clientCaFile = \"\"", "clientCaFile = \"../tls/missing-ca.crt\""), "tls.clientCaFile"},
            {"client certificate flag type", Replace(validConfig, "requireClientCertificate = false", "requireClientCertificate = \"true\""), "tls.requireClientCertificate"},
            {"required client certificate without CA", Replace(validConfig, "requireClientCertificate = false", "requireClientCertificate = true"), "tls.clientCaFile"},
            {"log level range", Replace(validConfig, "level = 2", "level = 4"), "logging.level"},
            {"log queue range", Replace(validConfig, "asyncQueueSize = 16", "asyncQueueSize = -1"), "logging.asyncQueueSize"},
            {"NUL database text", Replace(validConfig, "user = \"test-user\"", R"(user = "a\u0000b")"), "database.user"},
            {"NUL resource path", Replace(validConfig, "documentRoot = \"../site\"", R"(documentRoot = "../site\u0000tail")"), "server.documentRoot"},
            {"NUL TLS path", Replace(validConfig, "privateKey = \"../tls/server.key\"", R"(privateKey = "../tls/server.key\u0000tail")"), "tls.privateKey"},
            {"TOML syntax", "[server\nport = 8888", "TOML parse error at"},
        };

        for (const auto& test : cases) {
            runner.Run(test.name, [&] {
                const auto file = fixture.Write(test.text);
                bool rejected = false;
                try {
                    (void)LoadConfig(file);
                } catch (const std::runtime_error& error) {
                    rejected = true;
                    const std::string message = error.what();
                    Require(message.find(test.field) != std::string::npos, "error does not identify the field");
                    Require(message.find(file.string()) != std::string::npos, "error does not identify the config file");
                    Require(message.find("test-password") == std::string::npos, "error exposes a password");
                    Require(message.find("test-env-password") == std::string::npos, "error exposes an environment password");
                }
                Require(rejected, "invalid config was accepted");
            });
        }

        runner.Run("missing config file", [&] {
            bool rejected = false;
            try {
                (void)LoadConfig(fixture.root / "missing.toml");
            } catch (const std::runtime_error& error) {
                rejected = true;
                Require(std::string(error.what()).find("missing.toml") != std::string::npos, "missing config filename");
            }
            Require(rejected, "missing config was accepted");
        });

        runner.Run("empty config path", [&] {
            bool rejected = false;
            try {
                (void)LoadConfig({});
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            Require(rejected, "empty config path was accepted");
        });

        return runner.Finish();
    } catch (const std::exception& error) {
        std::cerr << "Test setup failed: " << error.what() << '\n';
        return 1;
    }
}
