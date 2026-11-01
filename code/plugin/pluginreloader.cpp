#include "pluginreloader.h"

#include "../log/log.h"

#include <stdexcept>
#include <utility>

using namespace std;

PluginReloader::PluginReloader(function<void()> reload) : reload_(std::move(reload)) {
    if (!reload_) {
        throw invalid_argument("Plugin reload callback must not be empty");
    }
    worker_ = thread(&PluginReloader::Run_, this);
}

PluginReloader::~PluginReloader() {
    {
        lock_guard<mutex> locker(mutex_);
        stopping_ = true;
        pending_ = false;
    }
    condition_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool PluginReloader::Request() {
    {
        lock_guard<mutex> locker(mutex_);
        if (stopping_) {
            return false;
        }
        pending_ = true;
    }
    condition_.notify_one();
    return true;
}

void PluginReloader::Run_() {
    unique_lock<mutex> locker(mutex_);
    while (true) {
        condition_.wait(locker, [this] { return stopping_ || pending_; });
        if (stopping_) {
            return;
        }
        pending_ = false;
        locker.unlock();
        try {
            reload_();
        } catch (const exception& error) {
            LOG_ERROR("Plugin reload failed: %s", error.what());
        } catch (...) {
            LOG_ERROR("Plugin reload failed: unknown exception");
        }
        locker.lock();
    }
}
