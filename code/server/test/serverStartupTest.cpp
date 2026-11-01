#include "../webserver.h"

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <pthread.h>
#include <sys/signalfd.h>
#include <thread>
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
int reloadFd = -1;
int reloadCloses = 0;
int epollWaitCalls = 0;

bool UsesPlugin() {
    return scenario.starts_with("plugin_") || scenario == "signal_fd_failure" ||
           scenario == "signal_epoll_failure" || scenario.starts_with("signal_runtime_");
}

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
        if (UsesPlugin()) {
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
            if (scenario.starts_with("signal_runtime_")) {
                filesystem::copy_file(pluginDirectory / "versionTwo.so", "version-two.so");
            }
        }
        if (scenario == "log_failure") {
            ofstream("log") << "not a directory";
        }
        ofstream config("server.toml");
        config << "[server]\ndocumentRoot = 'site'\nworkerThreads = 1\n"
               << "[database]\nuser = 'test-user'\npassword = 'test-password'\nname = 'testdb'\npoolSize = 2\n"
               << "[logging]\nenabled = " << (loggingEnabled ? "true" : "false")
               << "\nlevel = 1\nasyncQueueSize = " << (scenario == "signal_runtime_recover" ? 0 : 2) << "\n"
               << "[tls]\nenabled = " << (scenario == "tls_failure" ? "true" : "false")
               << "\ncertificate = 'server.crt'\nprivateKey = 'server.key'\n";
        if (UsesPlugin()) {
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

extern "C" int __real_pthread_sigmask(int, const sigset_t*, sigset_t*);
extern "C" int __wrap_pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
    if (scenario == "signal_mask_failure") {
        return EINVAL;
    }
    return __real_pthread_sigmask(how, set, old);
}
extern "C" int __real_signalfd(int, const sigset_t*, int);
extern "C" int __wrap_signalfd(int fd, const sigset_t* mask, int flags) {
    if (scenario == "signal_fd_failure") {
        errno = EMFILE;
        return -1;
    }
    reloadFd = __real_signalfd(fd, mask, flags);
    return reloadFd;
}

extern "C" int __wrap_mysql_server_init(int, char**, char**) {
    sigset_t mask{};
    Require(__real_pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0 && sigismember(&mask, SIGHUP) == 1,
            "MySQL library initialized before SIGHUP was blocked");
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
extern "C" int __real_epoll_ctl(int, int, int, epoll_event*);
extern "C" int __wrap_epoll_ctl(int epollFd, int operation, int fd, epoll_event* event) {
    if (scenario == "signal_fd_failure" || scenario == "signal_epoll_failure" || scenario.starts_with("signal_runtime_")) {
        if (fd == listener) {
            return 0;
        }
        if (scenario.starts_with("signal_runtime_")) {
            return __real_epoll_ctl(epollFd, operation, fd, event);
        }
    }
    errno = ENOMEM;
    return -1;
}
extern "C" int __real_epoll_wait(int, epoll_event*, int, int);
extern "C" int __wrap_epoll_wait(int fd, epoll_event* events, int maximum, int timeout) {
    if (!scenario.starts_with("signal_runtime_")) {
        return __real_epoll_wait(fd, events, maximum, timeout);
    }
    ++epollWaitCalls;
    const bool recover = scenario == "signal_runtime_recover";
    if (recover && epollWaitCalls == 2) {
        bool reported = false;
        for (int i = 0; i < 1500 && !reported; ++i) {
            for (const auto& entry : filesystem::directory_iterator("log")) {
                ifstream file(entry.path());
                const string text(istreambuf_iterator<char>(file), {});
                reported = reported || text.find("Plugin reload failed:") != string::npos;
            }
            if (!reported) {
                this_thread::sleep_for(chrono::milliseconds(2));
            }
        }
        Require(reported, "invalid runtime config was not reported");
        ifstream trace("lifecycle.log", ios::binary);
        const string events(istreambuf_iterator<char>(trace), {});
        Require(events == "C", "invalid runtime config changed the active plugin");
    }
    if (epollWaitCalls == 1 || (recover && epollWaitCalls == 2)) {
        ofstream config("server.toml");
        config << "[server]\ndocumentRoot = 'site'\n"
               << "[database]\nuser = 'test-user'\npassword = 'test-password'\nname = 'testdb'\n"
               << "[logging]\nenabled = false\n[plugin]\nenabled = true\npath = '"
               << (recover && epollWaitCalls == 1 ? "missing-runtime.so" : "version-two.so") << "'\n";
        config.close();
        Require(kill(getpid(), SIGHUP) == 0, "cannot send runtime reload signal");
        const int count = __real_epoll_wait(fd, events, maximum, 2000);
        Require(count == 1, "runtime SIGHUP did not reach epoll");
        return count;
    }
    for (int i = 0; i < 1500; ++i) {
        ifstream trace("lifecycle.log", ios::binary);
        const string recorded(istreambuf_iterator<char>(trace), {});
        if (recorded == "CCDU") {
            throw runtime_error("runtime reload test completed");
        }
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    throw runtime_error("runtime reload did not publish the new module");
}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) {
    if (fd == listener && listener >= 0) {
        ++listenerCloses;
    }
    if (fd == reloadFd && reloadFd >= 0) {
        ++reloadCloses;
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
        {"signal_mask_failure", "pthread_sigmask(SIGHUP) failed"},
        {"signal_fd_failure", "signalfd failed"},
        {"signal_epoll_failure", "epoll_ctl(ADD reload signal) failed"},
        {"signal_runtime_reload", "runtime reload test completed"},
        {"signal_runtime_recover", "runtime reload test completed"},
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
        loggingEnabled = scenario == "database_failure" || scenario == "log_failure" || scenario == "signal_runtime_recover";
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
        if (diagnostic.str().find(selected->second) == string::npos) {
            throw runtime_error("unexpected startup diagnostic: " + diagnostic.str());
        }
        Require(diagnostic.str().find("test-password") == string::npos, "startup printed the test password");
        Require(connections.empty(), "failed startup retained database connections");
        Require(FdCount() == before + (Log::Instance()->IsOpen() ? 1 : 0), "failed startup leaked a file descriptor");
        Require(listenerCloses == (listener >= 0 ? 1 : 0), "listener was not closed exactly once");
        Require(reloadCloses == (reloadFd >= 0 ? 1 : 0), "reload signal descriptor was not closed exactly once");
        if (scenario == "library_failure" || scenario == "log_failure") {
            Require(connectionAttempts == 0, "database connection was attempted before prerequisites succeeded");
        }
        if (scenario == "tls_failure" || scenario.starts_with("database") || scenario == "library_failure") {
            Require(socketCalls == 0, "startup continued to listening after a dependency failure");
        }
        if (UsesPlugin()) {
            ifstream trace(fixture.root / "lifecycle.log", ios::binary);
            const string events(istreambuf_iterator<char>(trace), {});
            string expected;
            if (scenario == "plugin_bad_abi") {
                expected = "U";
            } else if (scenario == "plugin_create_failure") {
                expected = "CU";
            } else if (scenario == "plugin_before_listen") {
                expected = "CDU";
            } else if (scenario == "signal_fd_failure" || scenario == "signal_epoll_failure") {
                expected = "CDU";
            } else if (scenario.starts_with("signal_runtime_")) {
                expected = "CCDUDU";
            }
            Require(events == expected, "plugin startup resources were not released in order");
            Require(socketCalls == (scenario == "plugin_before_listen" || scenario.starts_with("signal_") ? 1 : 0),
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
