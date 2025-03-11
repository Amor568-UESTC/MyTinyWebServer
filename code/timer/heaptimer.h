#pragma once

#include<queue>
#include<unordered_map>
#include<time.h>
#include<algorithm>
#include<arpa/inet.h>
#include<functional>
#include<assert.h>
#include<chrono>

#include"../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode
{
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& t) { return expires<t.expires;}
};

class HeapTimer
{
private:
    void del_(size_t i);
    void shiftup_(size_t i);
    bool shiftdown_(size_t idx,size_t n);
    void SwapNode_(size_t i,size_t j);

    std::vector<TimerNode> heap_;
    std::unordered_map<int,size_t> ref_;

public:
    HeapTimer() {heap_.reserve(64);}
    ~HeapTimer() {clear();}

    void adjust(int id,int newExpires);
    void add(int id,int timeOut,const TimeoutCallBack& cb);
    void doWork(int id);
    void clear();
    void tick();
    void pop();
    int GetNextTick();
};