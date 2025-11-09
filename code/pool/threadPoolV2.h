#pragma once 

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <any>

class AnyReturnPriTask {
private:
    struct BaseTask {
        virtual ~BaseTask() = default;
        virtual void execute() = 0;
        virtual std::any getFuture() = 0;
    };

    template<typename T>
    struct TaskImpl : BaseTask {
        std::function<T()> func;
        std::promise<T> promise;

        TaskImpl(std::function<T()> f) :
            func(std::move(f)) {}

        TaskImpl(const TaskImpl& ohter) :
            func(other.func) {}

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

        std::any getFuture() override {
            return std::any(promise.get_future());
        }
    };

    std::unique_ptr<BaseTask> _impl;
    unsigned int _pri : 2 = 1;

public:
    AnyReturnPriTask() = default;

    template<typename F, typename... Args>
    AnyReturnPriTask(unsigned int p, F&& f, Args&&... args) {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = [f = std::forward<F>(f), argsTuple = std::make_tuple(std::forward<Args>(args)...)] mutable -> ReturnType {
            return std::apply(f, std::move(argsTuple));
        };

        _impl = std::make_unique<TaskImpl<ReturnType>>(std::move(task), p);
    }

    AnyReturnPriTask(const AnyReturnPriTask& other) {
        if (other._impl) {
            _impl = std::make_unique<BaseTask>(*other._impl);
        } else {
            _impl.reset();
        }
    }

    AnyReturnPriTask& operator=(const AnyReturnPriTask& other) {
        if (this != &other) {
            if (other._impl) {
                _impl = std::make_unique<BaseTask>(*other._impl);
            } else {
                _impl.reset();
            }
        }
        return *this;
    }

    AnyReturnPriTask(AnyReturnPriTask&& other) noexcept = default;
    AnyReturnPriTask& operator=(AnyReturnPriTask&& other) noexcept = default;

    ~AnyReturnPriTask() noexcept = default;

    void execute() {
        _impl->execute();
    }

    template<typename T>
    std::future<T> getFuture() {
        return std::any_cast<std::future<T>>(_impl->getFuture());
    }

    auto operator<=>(const AnyReturnPriTask& other) {
        return _pri<=> other._pri;
    }

};

class ThreadPoolV2 {
private:
    std::vector<std::thread> _workers;

    // arranged in desc order of PriTask.pri
    std::priority_queue<AnyReturnPriTask> _tasks;
    std::mutex _mtx;
    std::condition_variable _cv;

    std::atomic<int> _activeThreads{0};
    std::atomic<bool> _stop = false;

    void work() {
        AnyReturnPriTask task;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(_mtx);
                _cv.wait(lock, [this]() {
                    return _stop || !_tasks.empty();
                });
                
                if (_stop && _tasks.empty()) {
                    break;
                }

                task = std::move(_tasks.top());
                _tasks.pop();
            }

            ++_activeThreads;
            task.execute();
            --_activeThreads;
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

    ~ThreadPoolV2() {
        shutdown();
    }

    template<typename F, typename... Args>
    auto submit(unsigned int pri, F&& f, Args&&... args) -> std::any {
        AnyReturnPriTask task(pri, std::forward<F>(f), std::forward<Args>(args)...);
        auto futureAny = task.getFuture<typename std::invoke_result_t<F, Args...>>();

        {
            std::lock_guard<std::mutex> lock(_mtx);
            _tasks.push(std::move(task));
        }

        _cv.notify_one();
        return futureAny;
    }
};