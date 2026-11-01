#include "pluginmanager.h"

#include <utility>

using namespace std;

shared_ptr<Module> PluginManager::Snapshot() const {
    return current_.load(memory_order_acquire);
}

void PluginManager::Reload(const string& path) {
    lock_guard<mutex> locker(reloadMutex_);
    auto candidate = Module::Load(path);
    //发布之前完成加载和初始化,失败不会替换当前版本
    current_.store(std::move(candidate), memory_order_release);
}
