#pragma once

#include<mutex>
#include<deque>
#include<condition_variable>
#include<sys/time.h>
#include<assert.h>

template<class T>
class BlockDeque
{
private:
    std::deque<T> deq_;
    size_t capacity_;
    std::mutex mtx_;
    bool isClose_;
    std::condition_variable condConsumer_;
    std::condition_variable condProducer_;

public:
    explicit BlockDeque(size_t MaxCapacity=1000);
    ~BlockDeque();

    void clear();
    bool empty();
    bool full();
    void Close();
    size_t size();
    size_t capacity();
    T front();
    T back();

    void push_back(const T& item);
    void push_front(const T& item);
    bool pop(T& item);
    bool pop(T& item,int timeout);
    void flush();
};

template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity):capacity_(MaxCapacity)
{
    assert(MaxCapacity>0);
    isClose_=0;
}

template<class T>
BlockDeque<T>::~BlockDeque()
{
    Close();
}

template<class T>
void BlockDeque<T>::clear() 
{
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

template<class T> 
bool BlockDeque<T>::empty() 
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

template<class T>
bool BlockDeque<T>::full()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size()>=capacity_;
}

template<class T>
void BlockDeque<T>::Close()
{
    {
        std::lock_guard<std::mutex> locker(mtx_);
        //关闭后禁止新写入,已有数据由消费者继续排空
        isClose_ = true;
    }
    condProducer_.notify_all();
    condConsumer_.notify_all();
}

template<class T>
size_t BlockDeque<T>::size()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

template<class T>
size_t BlockDeque<T>::capacity()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

template<class T>
T BlockDeque<T>::front()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

template<class T>
T BlockDeque<T>::back()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

template<class T>
void BlockDeque<T>::push_back(const T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    condProducer_.wait(locker, [this] {
        return isClose_ || deq_.size() < capacity_;
    });
    if (isClose_) {
        return;
    }
    deq_.push_back(item);
    condConsumer_.notify_one();
}

template<class T>
void BlockDeque<T>::push_front(const T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    condProducer_.wait(locker, [this] {
        return isClose_ || deq_.size() < capacity_;
    });
    if (isClose_) {
        return;
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

template<class T>
bool BlockDeque<T>::pop(T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    condConsumer_.wait(locker, [this] {
        return isClose_ || !deq_.empty();
    });
    if (deq_.empty()) {
        return false;
    }
    item=deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return 1;
}

template<class T>
bool BlockDeque<T>::pop(T& item,int timeout)
{
    std::unique_lock<std::mutex> locker(mtx_);
    if (!condConsumer_.wait_for(locker, std::chrono::seconds(timeout), [this] {
            return isClose_ || !deq_.empty();
        }) || deq_.empty()) {
        return false;
    }
    item=deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return 1;
}

template<class T>
void BlockDeque<T>::flush()
{
    condConsumer_.notify_one();
}
