#pragma once 

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <atomic>

struct BaseTask {
    virtual ~BaseTask() = default;
    virtual void execute() = 0;
    // virtual std::unique_ptr<BaseTask> clone() const = 0;
};

template<typename T>
struct TaskImpl : BaseTask {
    std::function<T()> func;
    std::promise<T> promise;

    TaskImpl(std::function<T()> f) : func(std::move(f)) {}

    // no copy for promise or future !!!
    TaskImpl(const TaskImpl& other) = delete;
    TaskImpl& operator=(const TaskImpl& other) = delete;

    TaskImpl(TaskImpl&& other) noexcept : func(std::move(other.func)), promise(std::move(other.promise)) {}

    TaskImpl& operator=(TaskImpl&& other) noexcept {
        if (this != & other) {
            func = std::move(other.func);
            promise = std::move(other.promise);
        }
        return *this;
    }

    // TaskImpl can be destroyed after execute called
    void execute() override {
        try {
            if constexpr (std::is_void_v<T>) {
                func();
                promise.set_value();
            } else {
                T res = func();
                promise.set_value(std::move(res));
            }
        } catch(...) {
            promise.set_exception(std::current_exception());
        }
    }

    std::future<T> getFuture() {
        return promise.get_future();
    }
};

class AnyReturnPriTask {
public:
    enum class Priority : unsigned int {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        URGENT = 3
    };

    static constexpr const char* pri2str(const Priority& pri) {
        switch (pri) {
            case Priority::LOW: return "LOW";
            case Priority::NORMAL: return "NORMAL";
            case Priority::HIGH: return "HIGH";
            case Priority::URGENT: return "URGENT";
        }
        return "UNKNOWN";
    }

#if __cplusplus >= 202002L
    static constexpr Priority str2pri(const char* str) {
        if (str == "LOW") return Priority::LOW;
        if (str == "HIGH") return Priority::HIGH;
        if (str == "URGENT") return Priority::URGENT;
        return Priority::NORMAL;
    }
#else
    static Priority str2pri(const char* str) {
        if (!strcmp(str, "LOW")) return Priority::LOW;
        if (!strcmp(str, "HIGH")) return Priority::HIGH;
        if (!strcmp(str, "URGENT")) return Priority::URGENT;
        return Priority::NORMAL;
    }
#endif

private:
    std::unique_ptr<BaseTask> _impl;
    Priority _pri = Priority::NORMAL;

public:
    AnyReturnPriTask() = default;

    template<typename F>
    AnyReturnPriTask(Priority p, F&& f) : _pri(p) {
        using returnType = std::invoke_result_t<F>;
        _impl = std::make_unique<TaskImpl<returnType>>(std::forward<F>(f));
    }

    template<typename F>
    AnyReturnPriTask(F&& f) : _pri(Priority::NORMAL) {
        using returnType = std::invoke_result_t<F>;
        _impl = std::make_unique<TaskImpl<returnType>>(std::forward<F>(f));
    }

    template<typename F, typename... Args>
    AnyReturnPriTask(Priority p, F&& f, Args&&... args) : _pri(p) {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = [f = std::forward<F>(f), argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType {
            return std::apply(f, std::move(argsTuple));
        };
        _impl = std::make_unique<TaskImpl<ReturnType>>(std::move(task));
    }
    
    template<typename F, typename... Args>
    AnyReturnPriTask(F&& f, Args&&... args) : _pri(Priority::NORMAL) {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = [f = std::forward<F>(f), argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType {
            return std::apply(f, std::move(argsTuple));
        };
        _impl = std::make_unique<TaskImpl<ReturnType>>(std::move(task));
    }

    AnyReturnPriTask(const AnyReturnPriTask& other) = delete;
    AnyReturnPriTask& operator=(const AnyReturnPriTask& other) = delete;

    AnyReturnPriTask(AnyReturnPriTask&& other) noexcept = default;
    AnyReturnPriTask& operator=(AnyReturnPriTask&& other) noexcept = default;

    ~AnyReturnPriTask() noexcept = default;

    void execute() {
        if (!_impl) {
            throw std::runtime_error("_impl is empty!");
        }
        _impl->execute();
    }

    template<typename T>
    std::future<T> getFuture() {
        if (!_impl) {
            throw std::runtime_error("_impl is empty!");
        }
        auto* concrete = dynamic_cast<TaskImpl<T>*>(_impl.get());
        if (concrete) {
            return concrete->getFuture();
        }
        throw std::bad_cast();
    }

#if __cpluscplus >= 202002L
    auto operator<=>(const AnyReturnPriTask& other) const {
        return static_cast<unsigned int>(_pri) <=> static_cast<unsigned int>(other._pri);
    }
#else
    bool operator<(const AnyReturnPriTask& other) const {
        return static_cast<unsigned int>(_pri) < static_cast<unsigned int>(other._pri);
    }
#endif

    const Priority& getPriority() const {
        return _pri;
    }

    const char* getPriorityStr() const {
        return pri2str(_pri);
    }

    bool empty() const {
        return _impl == nullptr;
    }
};

struct AnyReturnPriTaskPtrCmp {
    bool operator()(const AnyReturnPriTask* a, const AnyReturnPriTask* b) const {
        return *a < *b;
    }
};

class ThreadPoolV2 {
private:
    std::vector<std::thread> _workers;

    // arranged in desc order of PriTask.pri
    std::priority_queue<AnyReturnPriTask*, 
        std::vector<AnyReturnPriTask*>, 
        AnyReturnPriTaskPtrCmp
    > _tasks;
    std::mutex _mtx;
    std::condition_variable _cv;

    std::atomic<int> _activeThreads{0};
    std::atomic<bool> _stop{false};

    void work() {
        while (true) {
            AnyReturnPriTask* task;
            {
                std::unique_lock<std::mutex> lock(_mtx);
                _cv.wait(lock, [this]() {
                    return _stop || !_tasks.empty();
                });
                
                if (_stop && _tasks.empty()) {
                    break;
                }

                task = _tasks.top();
                _tasks.pop();
            }

            ++_activeThreads;
            task->execute();
            --_activeThreads;
            // can destroy because execute has been called
            delete task;
        }
        
    }

    void shutdown() {
        _stop = true;
        _cv.notify_all();
        
        for (auto& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

public:
    ThreadPoolV2() {
        size_t threadCnt = std::thread::hardware_concurrency();
        for (size_t i = 0; i < threadCnt; ++i) {
            _workers.emplace_back([this]() { work(); });
        }
    }

    ThreadPoolV2(size_t threadCnt) {
        for (size_t i = 0; i < threadCnt; ++i) {
            _workers.emplace_back([this]() { work(); });
        }
    }

    ~ThreadPoolV2() {
        shutdown();
    }

    template<typename F, typename... Args>
    auto submit(AnyReturnPriTask::Priority pri, F&& f, Args&&... args) {
        auto task = new AnyReturnPriTask(pri, std::forward<F>(f), std::forward<Args>(args)...);
        auto future = task->getFuture<typename std::invoke_result_t<F, Args...>>();

        {
            std::lock_guard<std::mutex> lock(_mtx);
            _tasks.push(task);
        }

        _cv.notify_one();
        return future;
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        return submit(AnyReturnPriTask::Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }

    size_t size() const {
        return _workers.size();
    }

    int getActiveThreads() const {
        return _activeThreads.load();
    }

    bool isStop() const {
        return _stop.load();
    }

    size_t pendingTasks() {
        std::lock_guard<std::mutex> lock(_mtx);
        return _tasks.size();
    }
};