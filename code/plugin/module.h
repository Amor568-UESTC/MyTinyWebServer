#pragma once

#include "plugin_api.h"

#include <memory>
#include <string>

//管理一个已加载的业务插件,不负责运行中的版本切换
class Module {
public:
    //全部校验和初始化成功后才返回,失败时抛异常并回收已取得的资源
    static std::shared_ptr<Module> Load(const std::string& path);
    ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    //返回主程序持有的正文,调用期间应保留Module的共享引用
    std::string Handle(const std::string& path) const;

private:
    Module() = default;

    void* handle_ = nullptr;
    const TinywebPluginApi* api_ = nullptr;
    void* instance_ = nullptr;
};
