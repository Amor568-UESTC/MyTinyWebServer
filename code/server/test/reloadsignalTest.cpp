#include "../reloadsignal.h"
#include "../epoller.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <pthread.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace std;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}
} //namespace

int main() {
    size_t passed = 0;
    const auto run = [&](const char* name, const function<void()>& operation) {
        operation();
        ++passed;
        cout << "PASS " << name << '\n';
    };
    try {
        ReloadSignal::Block();
        run("SIGHUP mask is inherited by new threads", [] {
            bool blocked = false;
            thread child([&] {
                sigset_t mask{};
                blocked = pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0 && sigismember(&mask, SIGHUP) == 1;
            });
            child.join();
            Require(blocked, "worker thread did not inherit the SIGHUP mask");
        });
        run("reload signal descriptor is nonblocking and close-on-exec", [] {
            ReloadSignal signal;
            Require((fcntl(signal.GetFd(), F_GETFL) & O_NONBLOCK) != 0, "missing O_NONBLOCK");
            Require((fcntl(signal.GetFd(), F_GETFD) & FD_CLOEXEC) != 0, "missing FD_CLOEXEC");
        });
        run("empty signal queue does not request a reload", [] {
            ReloadSignal signal;
            Require(!signal.Consume(), "empty signal queue reported work");
        });
        run("real SIGHUP is delivered through epoll and drained", [] {
            ReloadSignal signal;
            Epoller epoller;
            Require(epoller.AddFd(signal.GetFd(), EPOLLIN), "cannot register signalfd");
            Require(kill(getpid(), SIGHUP) == 0, "cannot send test signal");
            Require(epoller.Wait(1000) == 1 && epoller.GetEventFd(0) == signal.GetFd(), "signal event missing");
            Require(signal.Consume() && !signal.Consume() && epoller.Wait(0) == 0, "signal queue was not drained");
        });
        run("reload signal descriptor is closed on destruction", [] {
            int fd = -1;
            {
                ReloadSignal signal;
                fd = signal.GetFd();
            }
            Require(fcntl(fd, F_GETFD) < 0 && errno == EBADF, "signalfd was leaked");
        });
    } catch (const exception& error) {
        cerr << "FAIL signal check: " << error.what() << '\n';
        return 1;
    }
    cout << passed << " reload signal checks passed\n";
    return 0;
}
