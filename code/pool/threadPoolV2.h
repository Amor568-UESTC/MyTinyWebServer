#pragma once 

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <algorithm>

template<typename T>
struct PriTask {
    unsigned int pri : 2 = 1;
    std::function<T()> func;
    std::promise<T> promise;

    bool operator<=>(const PriTask& other) const {
        return pri <=> other.pri;
    }

    PriTask(std::function<T()> func, unsigned int pri = 1) :
        pri(pri), func(func) {}
};

template<typename F, typename... Args>
auto makePriTask(unsigned int pri, F&& f, Args&&... args) {
    using returnType = std::invoke_result_t<F, Args...>;

    PriTask<returnType> priTask(
        [f = std::forward<F>(f), argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            return std::apply(f, std::move(argsTuple));
        },
        pri
    );

    return priTask;
}

template<typename T>
class ThreadPoolV2 {
private:
    std::vector<std::thread> _workers;

    // arranged in desc order of PriTask.pri
    std::priority_queue<PriTask<T>> _tasks;
    std::mutex _mtx;
    std::condition_variable _cv;

    std::atomic<int> _activeThreads{0};
    std::atomic<bool> _stop = false;

    void work() {
        while (true) {
            PriTask<T> task;
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
            try {
                T ans = task.func();
                task.promise.set_value(ans);
            } catch(...) {
                task.promise.set_exception(std::current_exception());
            }
            --_activeThreads;
        }
    }

    void shutdown() {
        stop = true;
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
            _workers.emplace_back([this], work);
        }
    }

    ~ThreadPoolV2() {
        shutdown();
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args, unsigned int pri = 1) -> std::future<T> {
        PriTask<T> task = makePriTask<F, Args>(f, args, pri);
        auto future = task.promise.get_future();

        {
            std::unique_lock<std::mutex> lock(_mtx);
            _tasks.push(std::move(task));
        }

        _cv.notify_one();
        return future;
    }
};