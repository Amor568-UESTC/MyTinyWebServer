#include "../webserver.h"

#include <algorithm>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace std;

int TinywebServerMain(int argc, char* argv[]);

namespace {
string scenario;
unordered_set<MYSQL*> connections;
int socketCalls = 0;
int listener = -1;
int listenerCloses = 0;
int connectionAttempts = 0;
bool loggingEnabled = false;
filesystem::path pluginDirectory;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

size_t FdCount() {
    return distance(filesystem::directory_iterator("/proc/self/fd"), filesystem::directory_iterator{});
}

struct Fixture {
    filesystem::path root;
    filesystem::path previous = filesystem::current_path();

    Fixture() {
        char pattern[] = "/tmp/tinyweb-startup-test-XXXXXX";
        const char* directory = mkdtemp(pattern);
        Require(directory != nullptr, "mkdtemp failed");
        root = directory;
        filesystem::current_path(root);
        filesystem::create_directory("site");
        ofstream("server.crt") << "invalid test certificate";
        ofstream("server.key") << "invalid test key";
        if (scenario.starts_with("plugin_")) {
            if (scenario == "plugin_invalid_file") {
                ofstream("plugin.so") << "invalid shared library";
            } else if (scenario != "plugin_missing_file") {
                string name = "valid";
                if (scenario == "plugin_bad_abi") {
                    name = "badAbi";
                } else if (scenario == "plugin_create_failure") {
                    name = "createFailure";
                }
                filesystem::copy_file(pluginDirectory / (name + ".so"), "plugin.so");
            }
            Require(setenv("TINYWEB_PLUGIN_TEST_TRACE", (root / "lifecycle.log").c_str(), 1) == 0,
                    "cannot set plugin trace path");
        }
        if (scenario == "log_failure") {
            ofstream("log") << "not a directory";
        }
        ofstream config("server.toml");
        config << "[server]\ndocumentRoot = 'site'\nworkerThreads = 1\n"
               << "[database]\nuser = 'test-user'\npassword = 'test-password'\nname = 'testdb'\npoolSize = 2\n"
               << "[logging]\nenabled = " << (loggingEnabled ? "true" : "false")
               << "\nlevel = 1\nasyncQueueSize = 2\n"
               << "[tls]\nenabled = " << (scenario == "tls_failure" ? "true" : "false")
               << "\ncertificate = 'server.crt'\nprivateKey = 'server.key'\n";
        if (scenario.starts_with("plugin_")) {
            config << "[plugin]\nenabled = true\npath = 'plugin.so'\n";
        }
    }

    ~Fixture() {
        error_code ignored;
        filesystem::current_path(previous, ignored);
        filesystem::remove_all(root, ignored);
    }
};
}

extern "C" int __wrap_mysql_server_init(int, char**, char**) {
    return scenario == "library_failure" ? 1 : 0;
}
extern "C" MYSQL* __wrap_mysql_init(MYSQL*) {
    MYSQL* sql = new MYSQL{};
    connections.insert(sql);
    return sql;
}
extern "C" MYSQL* __wrap_mysql_real_connect(MYSQL* sql, const char*, const char*, const char*,
                                           const char*, unsigned int, const char*, unsigned long) {
    ++connectionAttempts;
    Require(Log::Instance()->IsOpen() == loggingEnabled, "database connection preceded log initialization");
    return scenario == "database_failure" || scenario == "database_without_logs" ? nullptr : sql;
}
extern "C" const char* __wrap_mysql_error(MYSQL*) {
    return "simulated authentication failure";
}
extern "C" unsigned int __wrap_mysql_errno(MYSQL*) {
    return 1045;
}
extern "C" void __wrap_mysql_close(MYSQL* sql) {
    if (connections.erase(sql) != 1) {
        abort();
    }
    delete sql;
}

extern "C" int __wrap_socket(int, int, int) {
    ++socketCalls;
    if (scenario == "socket_failure") {
        errno = EACCES;
        return -1;
    }
    //使用真实文件fd检查所有权,不创建或绑定网络端口
    listener = open("/dev/null", O_RDWR | O_CLOEXEC);
    return listener;
}
extern "C" int __wrap_setsockopt(int, int, int option, const void*, socklen_t) {
    if ((scenario == "linger_failure" && option == SO_LINGER) ||
        (scenario == "reuse_failure" && option == SO_REUSEADDR)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
extern "C" int __wrap_bind(int, const sockaddr*, socklen_t) {
    if (scenario == "bind_failure") {
        errno = EADDRINUSE;
        return -1;
    }
    return 0;
}
extern "C" int __wrap_listen(int, int) {
    if (scenario == "listen_failure") {
        errno = EOPNOTSUPP;
        return -1;
    }
    return 0;
}
extern "C" int __real_fcntl(int, int, ...);
extern "C" int __wrap_fcntl(int fd, int operation, ...) {
    if (operation == F_GETFL) {
        return __real_fcntl(fd, operation);
    }
    if (scenario == "nonblock_failure" && operation == F_SETFL) {
        errno = EIO;
        return -1;
    }
    va_list arguments;
    va_start(arguments, operation);
    const int flags = va_arg(arguments, int);
    va_end(arguments);
    return __real_fcntl(fd, operation, flags);
}
extern "C" int __real_epoll_create1(int);
extern "C" int __wrap_epoll_create1(int flags) {
    if (scenario == "epoll_create_failure") {
        errno = EMFILE;
        return -1;
    }
    return __real_epoll_create1(flags);
}
extern "C" int __wrap_epoll_ctl(int, int, int, epoll_event*) {
    errno = ENOMEM;
    return -1;
}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) {
    if (fd == listener && listener >= 0) {
        ++listenerCloses;
    }
    return __real_close(fd);
}

int main(int argc, char* argv[]) {
    const vector<pair<string, string>> cases = {
        {"library_failure", "MySQL library initialization failed"},
        {"log_failure", "filesystem error"},
        {"database_failure", "MySQL connect failed"},
        {"database_without_logs", "MySQL connect failed"},
        {"tls_failure", "TLS initialization failed: Cannot load the TLS certificate"},
        {"socket_failure", "socket failed"},
        {"linger_failure", "setsockopt(SO_LINGER) failed"},
        {"reuse_failure", "setsockopt(SO_REUSEADDR) failed"},
        {"nonblock_failure", "fcntl(O_NONBLOCK) failed"},
        {"bind_failure", "bind port 8888 failed"},
        {"listen_failure", "listen failed"},
        {"epoll_failure", "epoll_ctl(ADD listener) failed"},
        {"epoll_create_failure", "epoll_create1 failed"},
        {"plugin_missing_file", "plugin.path"},
        {"plugin_invalid_file", "dlopen failed"},
        {"plugin_bad_abi", "Plugin ABI version or structure size mismatch"},
        {"plugin_create_failure", "Plugin create failed"},
        {"plugin_before_listen", "epoll_ctl(ADD listener) failed"},
    };
    if (argc != 2 && argc != 3) {
        return 2;
    }
    scenario = argv[1];
    if (scenario == "--list") {
        for (const auto& item : cases) {
            cout << item.first << '\n';
        }
        return 0;
    }
    try {
        if (argc == 3) {
            pluginDirectory = filesystem::absolute(argv[2]);
        }
        const auto selected = find_if(cases.begin(), cases.end(), [](const auto& item) {
            return item.first == scenario;
        });
        Require(selected != cases.end(), "unknown test case");
        loggingEnabled = scenario == "database_failure" || scenario == "log_failure";
        Fixture fixture;
        const size_t before = FdCount();
        string program = "server";
        string option = "--config";
        string config = "server.toml";
        char* arguments[] = {program.data(), option.data(), config.data(), nullptr};
        ostringstream diagnostic;
        auto* previous = cerr.rdbuf(diagnostic.rdbuf());
        const int result = TinywebServerMain(3, arguments);
        cerr.rdbuf(previous);
        Require(result == 1, "failed startup did not return exit code 1");
        Require(diagnostic.str().find(selected->second) != string::npos, "startup lost its failure reason");
        Require(diagnostic.str().find("test-password") == string::npos, "startup printed the test password");
        Require(connections.empty(), "failed startup retained database connections");
        Require(FdCount() == before + (Log::Instance()->IsOpen() ? 1 : 0), "failed startup leaked a file descriptor");
        Require(listenerCloses == (listener >= 0 ? 1 : 0), "listener was not closed exactly once");
        if (scenario == "library_failure" || scenario == "log_failure") {
            Require(connectionAttempts == 0, "database connection was attempted before prerequisites succeeded");
        }
        if (scenario == "tls_failure" || scenario.starts_with("database") || scenario == "library_failure") {
            Require(socketCalls == 0, "startup continued to listening after a dependency failure");
        }
        if (scenario.starts_with("plugin_")) {
            ifstream trace(fixture.root / "lifecycle.log", ios::binary);
            const string events(istreambuf_iterator<char>(trace), {});
            string expected;
            if (scenario == "plugin_bad_abi") {
                expected = "U";
            } else if (scenario == "plugin_create_failure") {
                expected = "CU";
            } else if (scenario == "plugin_before_listen") {
                expected = "CDU";
            }
            Require(events == expected, "plugin startup resources were not released in order");
            Require(socketCalls == (scenario == "plugin_before_listen" ? 1 : 0),
                    "listener was opened before the plugin was usable");
            if (scenario == "plugin_missing_file") {
                Require(connectionAttempts == 0, "invalid plugin path reached database initialization");
            }
        }
        cout << "PASS " << scenario << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "FAIL " << scenario << ": " << error.what() << '\n';
        return 1;
    }
}
