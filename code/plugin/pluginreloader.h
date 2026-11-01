#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

//独立执行重载,合并忙碌期间重复提交的请求,析构时等待当前任务结束
class PluginReloader {
public:
    explicit PluginReloader(std::function<void()> reload);
    ~PluginReloader();

    PluginReloader(const PluginReloader&) = delete;
    PluginReloader& operator=(const PluginReloader&) = delete;
    PluginReloader(PluginReloader&&) = delete;
    PluginReloader& operator=(PluginReloader&&) = delete;

    bool Request();

private:
    void Run_();

    std::function<void()> reload_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stopping_ = false;
    bool pending_ = false;
    std::thread worker_;
};
