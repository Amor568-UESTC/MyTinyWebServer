#pragma once

#include<mutex>
#include<condition_variable>
#include<queue>
#include<thread>
#include<functional>

struct Pool
{
    std::mutex mtx;
    std::condition_variable cond;
    bool isClosed;
    std::queue<std::function<void()>> tasks;
};

class ThreadPool
{
private:
    std::shared_ptr<Pool> pool_;
    
public:
    explicit ThreadPool(size_t threadCnt=8):pool_(std::make_shared<Pool>())
    {
        assert(threadCnt>0);
        for(size_t i=0;i<threadCnt;i++)
            std::thread([pool=pool_]{
                std::unique_lock<std::mutex> locker(pool->mtx);
                while(1)
                {
                    if(!pool->tasks.empty())
                    {
                        auto task=std::move(pool->tasks.front());
                        pool->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock();
                    }
                    else if(pool->isClosed) break;
                    else pool->cond.wait(locker);
                }
            }).detach();
    }

    ThreadPool()=default;
    ThreadPool(ThreadPool&&)=default;

    ~ThreadPool()
    {
        if(static_cast<bool>(pool_))
        {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed=true;
            }
            pool_->cond.notify_all();
        }
    }

    template<class F>
    void AddTask(F&& task)
    {
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);
            pool_->tasks.emplace(std::forward<F>(task));
        }
        pool_->cond.notify_one();
    }
};