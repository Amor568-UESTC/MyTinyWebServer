#include "../configloader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
            Require(config.logging.enabled && config.logging.level == 2, "logging enabled or level");
            Require(config.logging.asyncQueueSize == 16, "asyncQueueSize");
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
            Require(config.logging.enabled && config.logging.level == 1 &&
                    config.logging.asyncQueueSize == 1024, "logging defaults");
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

        const std::vector<InvalidCase> cases = {
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
