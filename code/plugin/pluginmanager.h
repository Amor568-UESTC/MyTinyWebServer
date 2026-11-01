#pragma once

#include "module.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

class PluginManager {
public:
    //每次业务分派只取得一次快照,旧快照继续持有旧模块
    std::shared_ptr<Module> Snapshot() const;
    //失败抛异常,当前模块保持不变,多个重载串行执行
    void Reload(const std::string& path);

private:
    std::atomic<std::shared_ptr<Module>> current_{nullptr};
    std::mutex reloadMutex_;
};
