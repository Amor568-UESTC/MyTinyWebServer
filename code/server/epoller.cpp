#include"epoller.h"

#include <stdexcept>
#include <system_error>

using namespace std;

Epoller::Epoller(int maxEvent) : epollFd_(-1)
{
    if (maxEvent <= 0) {
        throw invalid_argument("epoll event capacity must be positive");
    }
    events_.resize(static_cast<size_t>(maxEvent));
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        throw system_error(errno, generic_category(), "epoll_create1 failed");
    }
}

Epoller::~Epoller() { close(epollFd_);}

bool Epoller::AddFd(int fd,uint32_t events)
{
    if(fd<0) return 0;
    epoll_event ev={0};
    ev.data.fd=fd;
    ev.events=events;
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_ADD,fd,&ev);
}

bool Epoller::ModFd(int fd,uint32_t events)
{
    if(fd<0) return 0;
    epoll_event ev={0};
    ev.data.fd=fd;
    ev.events=events;
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_MOD,fd,&ev);
}

bool Epoller::DelFd(int fd)
{
    if(fd<0) return 0;
    epoll_event ev={0};
    return 0==epoll_ctl(epollFd_,EPOLL_CTL_DEL,fd,&ev);
}

int Epoller::Wait(int timeoutMS)
{
    return epoll_wait(epollFd_,&events_[0],static_cast<int>(events_.size()),timeoutMS);
}

int Epoller::GetEventFd(size_t i) const
{
    assert(i<events_.size()&&i>=0);
    return events_[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i) const
{
    assert(i<events_.size()&&i>=0);
    return events_[i].events;
}
