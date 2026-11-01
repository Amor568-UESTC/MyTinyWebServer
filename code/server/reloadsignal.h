#pragma once

//主线程先屏蔽SIGHUP,再创建任何线程,所有子线程继承屏蔽状态
class ReloadSignal {
public:
    static void Block();
    ReloadSignal();
    ~ReloadSignal();

    ReloadSignal(const ReloadSignal&) = delete;
    ReloadSignal& operator=(const ReloadSignal&) = delete;
    ReloadSignal(ReloadSignal&&) = delete;
    ReloadSignal& operator=(ReloadSignal&&) = delete;

    int GetFd() const { return fd_; }
    bool Consume();

private:
    int fd_ = -1;
};
