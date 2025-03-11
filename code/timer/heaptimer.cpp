#include"heaptimer.h"

using namespace std;

void HeapTimer::del_(size_t i)
{
    assert(!heap_.empty()&&i>=0&&i<heap_.size());
    size_t n=heap_.size()-1;
    if(i<n)
    {
        SwapNode_(i,n);
        if(!shiftdown_(i,n))
            shiftup_(i);
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void HeapTimer::shiftup_(size_t i)
{
    assert(i>=0&&i<heap_.size());
    while(i>0)
    {
        size_t parent=(i-1)/2;
        if(heap_[parent]<heap_[i])
            break;
        SwapNode_(i,parent);
        i=parent;
    }
}

bool HeapTimer::shiftdown_(size_t idx,size_t n)
{
    assert(idx>=0&&idx<heap_.size());
    assert(n>=0&&n<=heap_.size());
    size_t i=idx;
    while(i*2+1<n)
    {
        size_t left=i*2+1;
        size_t right=i*2+2;
        size_t older=left;
        if(right<n&&heap_[right]<heap_[older])
            older=right;
        if(heap_[i]<heap_[older])
            break;
        SwapNode_(i,older);
        i=older;
    }
    return i>idx;
}

void HeapTimer::SwapNode_(size_t i,size_t j)
{
    assert(i>=0&&i<heap_.size());
    assert(j>=0&&j<heap_.size());
    swap(heap_[i],heap_[j]);
    ref_[heap_[i].id]=i;
    ref_[heap_[j].id]=j;
}

void HeapTimer::adjust(int id,int newExpires)
{
    assert(!heap_.empty()&&ref_.count(id));
    heap_[ref_[id]].expires=Clock::now()+MS(newExpires);
    shiftdown_(ref_[id],heap_.size());
}

void HeapTimer::add(int id,int timeOut,const TimeoutCallBack& cb)
{
    assert(id>=0);
    size_t i;
    if(!ref_.count(id))
    {
        i=heap_.size();
        ref_[id]=i;
        heap_.push_back({id,Clock::now()+MS(timeOut),cb});
        shiftup_(i);
    }
    else
    {
        i=ref_[id];
        heap_[i].expires=Clock::now()+MS(timeOut);
        heap_[i].cb=cb;
        if(!shiftdown_(i,heap_.size()))
            shiftup_(i);
    }
}

void HeapTimer::doWork(int id)
{
    if(heap_.empty()||!ref_.count(id))
        return ;
    size_t i=ref_[id];
    TimerNode cur=heap_[i];
    cur.cb();
    del_(i);
}

void HeapTimer::clear()
{
    ref_.clear();
    heap_.clear();
}

void HeapTimer::tick()
{
    if(heap_.empty()) return ;
    while(!heap_.empty())
    {
        TimerNode cur=heap_.front();
        if(chrono::duration_cast<MS>(cur.expires-Clock::now()).count()) //未超时
            break;
        cur.cb();
        pop();
    }
}

void HeapTimer::pop()
{
    assert(!heap_.empty());
    del_(0);
}

int HeapTimer::GetNextTick()
{
    tick();
    size_t res=-1;
    if(!heap_.empty())
    {
        res=chrono::duration_cast<MS>(heap_.front().expires-Clock::now()).count();
        if(res<0) res=0;
    }
    return res;
}