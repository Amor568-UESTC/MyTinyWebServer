#pragma once

#include<mutex>
#include<string>
#include<thread>
#include<sys/time.h>
#include<string.h>
#include<stdarg.h>
#include<assert.h>
#include<sys/stat.h>

#include"blockqueue.hpp"
#include"../buffer/buffer.h"

class Log
{
private:
    Log();
    virtual ~Log();
    void AppendLogLevelTitle_(int level);
    void AsyncWrite_();

    static const int LOG_PATH_LEN=256;
    static const int LOG_NAME_LEN=256;
    static const int MAX_LINES=50000;

    const char* path_;
    const char* suffix_;
    int MAX_LINES_;
    int lineCnt_;
    int toDay_;
    int level_;
    bool isOpen_;
    bool isAsync_;

    Buffer buff_;
    FILE* fp_;
    std::unique_ptr<BlockDeque<std::string>> deque_;
    std::unique_ptr<std::thread> writeThread_;
    std::mutex mtx_;

public:
    void init(int level=1,const char* path="./log",const char* suffix=".log",int maxQueueCapacity=1024);
    static Log* Instance();
    static void FlushLogThread();
    void write(int level,const char* format,...);
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen() {return isOpen_;}
};

#define LOG_BASE(level,format,...) \
    do \
    {\
        Log* log=Log::Instance();\
        if(log->IsOpen()&&log->GetLevel()>=level) \
        {\
            log->write(level,format,##__VA_ARGS__); \
            log->flush(); \
        }\
    }while(0); \

#define LOG_DEBUG(format,...) do {LOG_BASE(0,format,##__VA_ARGS__)} while(0)
#define LOG_INFO(format,...) do {LOG_BASE(1,format,##__VA_ARGS__)} while(0)
#define LOG_WARN(format,...) do {LOG_BASE(2,format,##__VA_ARGS__)} while(0)
#define LOG_ERROR(format,...) do {LOG_BASE(3,format,##__VA_ARGS__)} while(0)