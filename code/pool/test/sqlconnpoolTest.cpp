#include "../sqlconnRAII.h"
#include "../../http/httprequest.h"
#include "../../handler/requesthandler.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {
std::mutex fakeMutex;
std::unordered_set<MYSQL*> alive;
int initCalls = 0;
int connectCalls = 0;
int failInitAt = 0;
int failConnectAt = 0;
bool failLibrary = false;
bool failThread = false;
std::atomic<int> threadStarts{0};
std::atomic<int> threadEnds{0};
int closeCalls = 0;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void Init(SqlConnPool* pool, int size = 2) {
    pool->Init("localhost", 3306, "test-user", "test-password", "testdb", size);
}
void LoginWithoutConnection() {
    const std::string body = "username=test-user&password=test-password";
    const std::string request = "POST /login HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\n\r\n" + body;
    Buffer buffer;
    buffer.Append(request);
    HttpRequest parser;
    Require(parser.parse(buffer) == HttpRequest::ParseRes::Complete, "login parsing failed");
    Require(parser.path() == "/login", "parser rewrote the business route");
    const RequestHandler handler;
    Require(handler.Handle(parser).path == "/error.html", "missing connection did not produce a business failure");
}
}

extern "C" int __wrap_mysql_server_init(int, char**, char**) {
    return failLibrary ? 1 : 0;
}
extern "C" MYSQL* __wrap_mysql_init(MYSQL*) {
    ++initCalls;
    if (initCalls == failInitAt) {
        return nullptr;
    }
    MYSQL* sql = new MYSQL{};
    std::lock_guard<std::mutex> lock(fakeMutex);
    alive.insert(sql);
    return sql;
}
extern "C" MYSQL* __wrap_mysql_real_connect(MYSQL* sql, const char*, const char*, const char*, const char*, unsigned int, const char*, unsigned long) {
    ++connectCalls;
    return connectCalls == failConnectAt ? nullptr : sql;
}
extern "C" const char* __wrap_mysql_error(MYSQL*) {
    return "simulated authentication failure";
}
extern "C" unsigned int __wrap_mysql_errno(MYSQL*) {
    return 1698;
}
extern "C" void __wrap_mysql_close(MYSQL* sql) {
    std::lock_guard<std::mutex> lock(fakeMutex);
    if (alive.erase(sql) != 1) {
        std::abort();
    }
    ++closeCalls;
    delete sql;
}
extern "C" auto __wrap_mysql_thread_init() -> decltype(mysql_thread_init()) {
    ++threadStarts;
    return failThread;
}
extern "C" void __wrap_mysql_thread_end() {
    ++threadEnds;
}

int main(int argc, char* argv[]) {
    static_assert(!std::is_copy_constructible_v<SqlConnRAII>);
    static_assert(!std::is_move_constructible_v<SqlConnRAII>);
    if (argc != 2) {
        return 2;
    }
    try {
        const std::string name = argv[1];
        const std::vector<std::string> cases = {
            "library_failure",
            "uninitialized",
            "init_failure",
            "connect_failure_retry",
            "borrow_return",
            "guard",
            "early_return",
            "close_with_lease",
            "duplicate_init",
            "concurrent",
            "thread_state",
            "thread_failure",
            "login_no_pool",
            "login_busy_pool",
            "invalid_size",
        };
        if (name == "--list") {
            for (const auto& item : cases) {
                std::cout << item << '\n';
            }
            return 0;
        }
        Require(std::find(cases.begin(), cases.end(), name) != cases.end(), "unknown test case");
        if (name == "library_failure") {
            failLibrary = true;
            bool rejected = false;
            try {
                SqlConnPool::Instance();
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            Require(rejected, "library initialization failure was ignored");
            failLibrary = false;
        }
        auto* pool = SqlConnPool::Instance();
        if (name == "uninitialized") {
            Require(pool->GetConn() == nullptr, "uninitialized pool handed out a connection");
        } else if (name == "init_failure") {
            failInitAt = 3;
            bool rejected = false;
            try {
                Init(pool, 4);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            Require(rejected && alive.empty() && closeCalls == 2, "mysql_init failure did not roll back");
            Require(pool->GetFreeConnCnt() == 0 && pool->GetConn() == nullptr, "partial pool became visible");
        } else if (name == "connect_failure_retry") {
            failConnectAt = 3;
            bool rejected = false;
            try {
                Init(pool, 4);
            } catch (const std::runtime_error& error) {
                const std::string message = error.what();
                rejected = message.find("1698") != std::string::npos && message.find("test-password") == std::string::npos;
            }
            Require(rejected && alive.empty() && closeCalls == 3, "connection failure did not roll back");
            Init(pool);
            Require(pool->GetFreeConnCnt() == 2, "retry after failure did not work");
        } else if (name == "borrow_return") {
            Init(pool);
            MYSQL* first = pool->GetConn();
            MYSQL* second = pool->GetConn();
            Require(first != nullptr && second != nullptr && first != second, "borrowed invalid connections");
            Require(pool->GetConn() == nullptr, "exhausted pool did not fail immediately");
            pool->FreeConn(first);
            pool->FreeConn(second);
            pool->FreeConn(nullptr);
            Require(pool->GetFreeConnCnt() == 2, "incorrect return count");
        } else if (name == "guard") {
            Init(pool, 1);
            {
                MYSQL* sql = nullptr;
                SqlConnRAII guard(&sql, pool);
                Require(sql != nullptr && pool->GetConn() == nullptr, "guard returned its connection too early");
            }
            Require(pool->GetFreeConnCnt() == 1, "guard did not return its connection once");
        } else if (name == "early_return") {
            Init(pool, 1);
            const auto run = [&]() {
                MYSQL* sql = nullptr;
                SqlConnRAII guard(&sql, pool);
                Require(sql != nullptr, "early-return guard could not acquire");
                return false;
            };
            Require(!run() && pool->GetFreeConnCnt() == 1, "early return leaked a connection");
        } else if (name == "close_with_lease") {
            Init(pool);
            MYSQL* sql = pool->GetConn();
            pool->ClosePool();
            pool->ClosePool();
            Require(alive.size() == 1 && pool->GetConn() == nullptr, "close released a borrowed connection or still allows borrowing");
            pool->FreeConn(sql);
            Require(alive.empty() && closeCalls == 2, "late return did not close the borrowed connection");
        } else if (name == "duplicate_init") {
            Init(pool);
            MYSQL* sql = pool->GetConn();
            bool rejected = false;
            try {
                Init(pool);
            } catch (const std::logic_error&) {
                rejected = true;
            }
            Require(rejected && initCalls == 2, "duplicate initialization changed the pool");
            pool->FreeConn(sql);
        } else if (name == "concurrent") {
            Init(pool, 3);
            std::mutex mutex;
            std::unordered_set<MYSQL*> inUse;
            std::atomic<bool> valid{true};
            std::vector<std::thread> threads;
            for (int i = 0; i < 12; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < 200; ++j) {
                        MYSQL* sql = pool->GetConn();
                        if (sql == nullptr) {
                            std::this_thread::yield();
                            continue;
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (!inUse.insert(sql).second) {
                                valid = false;
                            }
                        }
                        std::this_thread::yield();
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            inUse.erase(sql);
                        }
                        pool->FreeConn(sql);
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            Require(valid && inUse.empty() && pool->GetFreeConnCnt() == 3, "concurrent borrowing violated exclusive ownership");
        } else if (name == "thread_state") {
            Init(pool, 2);
            std::vector<std::thread> threads;
            for (int i = 0; i < 2; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < 3; ++j) {
                        MYSQL* sql = nullptr;
                        SqlConnRAII guard(&sql, pool);
                        Require(sql != nullptr, "thread guard failed");
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            Require(threadStarts == 2 && threadEnds == 2, "MySQL thread state did not match thread lifetime");
        } else if (name == "thread_failure") {
            Init(pool, 1);
            failThread = true;
            MYSQL* sql = reinterpret_cast<MYSQL*>(1);
            SqlConnRAII guard(&sql, pool);
            Require(sql == nullptr && pool->GetFreeConnCnt() == 1, "thread initialization failure consumed a connection");
        } else if (name == "login_no_pool") {
            LoginWithoutConnection();
        } else if (name == "login_busy_pool") {
            Init(pool, 1);
            MYSQL* sql = pool->GetConn();
            LoginWithoutConnection();
            Require(pool->GetFreeConnCnt() == 0, "failed login returned another request's connection");
            pool->FreeConn(sql);
        } else if (name == "invalid_size") {
            bool rejected = false;
            try {
                Init(pool, 0);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Require(rejected && initCalls == 0, "invalid size reached MySQL initialization");
        }
        pool->ClosePool();
        Require(alive.empty(), "connection handles leaked");
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
