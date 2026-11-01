#include "reloadsignal.h"

#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <stdexcept>
#include <sys/signalfd.h>
#include <system_error>
#include <unistd.h>

using namespace std;

namespace {
sigset_t SighupMask() {
    sigset_t mask{};
    if (sigemptyset(&mask) != 0 || sigaddset(&mask, SIGHUP) != 0) {
        throw system_error(errno, generic_category(), "Cannot create SIGHUP mask");
    }
    return mask;
}
} //namespace

void ReloadSignal::Block() {
    const auto mask = SighupMask();
    const int error = pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    if (error != 0) {
        throw system_error(error, generic_category(), "pthread_sigmask(SIGHUP) failed");
    }
}

ReloadSignal::ReloadSignal() {
    const auto mask = SighupMask();
    fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (fd_ < 0) {
        throw system_error(errno, generic_category(), "signalfd failed");
    }
}

ReloadSignal::~ReloadSignal() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool ReloadSignal::Consume() {
    bool requested = false;
    while (true) {
        signalfd_siginfo info{};
        const ssize_t size = read(fd_, &info, sizeof(info));
        if (size == static_cast<ssize_t>(sizeof(info))) {
            requested = requested || info.ssi_signo == SIGHUP;
            continue;
        }
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size < 0 && errno == EAGAIN) {
            return requested;
        }
        if (size < 0) {
            throw system_error(errno, generic_category(), "read(signalfd) failed");
        }
        throw runtime_error("signalfd returned an incomplete signal record");
    }
}
