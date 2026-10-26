#include "log.h"

#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

using namespace std;

Log::Log()
    : lineCnt_(0), toDay_(0), level_(1), isOpen_(false), isAsync_(false), fp_(nullptr)
{
}

Log::~Log()
{
    {
        lock_guard<mutex> locker(mtx_);
        isOpen_ = false;
    }
    if (writeThread_ && writeThread_->joinable()) {
        //关闭后继续消费已有日志,不再通过忙等判断队列是否排空
        deque_->Close();
        writeThread_->join();
    }
    if (fp_) {
        fflush(fp_);
        fclose(fp_);
    }
}

void Log::AppendLogLevelTitle_(int level)
{
    switch (level) {
        case 0:
            buff_.Append("[debug]: ");
            break;
        case 1:
            buff_.Append("[info] : ");
            break;
        case 2:
            buff_.Append("[warn] : ");
            break;
        case 3:
            buff_.Append("[error]: ");
            break;
        default:
            buff_.Append("[info] : ");
            break;
    }
}

void Log::AsyncWrite_()
{
    string text;
    while (deque_->pop(text)) {
        lock_guard<mutex> locker(mtx_);
        fwrite(text.data(), 1, text.size(), fp_);
    }
}

void Log::init(int level, const char* path, const char* suffix, int maxQueueCapacity)
{
    lock_guard<mutex> locker(mtx_);
    if (isOpen_) {
        throw logic_error("Logger is already initialized");
    }
    if (level < 0 || level > 3 || maxQueueCapacity < 0 ||
        path == nullptr || path[0] == '\0' || suffix == nullptr) {
        throw invalid_argument("Invalid logging configuration");
    }

    string newPath(path);
    string newSuffix(suffix);
    filesystem::create_directories(newPath);
    const time_t timer = time(nullptr);
    tm calendar{};
    if (localtime_r(&timer, &calendar) == nullptr) {
        throw runtime_error("Cannot obtain the log date");
    }
    char date[32]{};
    strftime(date, sizeof(date), "%Y_%m_%d", &calendar);
    const auto fileName = filesystem::path(newPath) / (string(date) + newSuffix);

    //文件和队列均准备成功后,再创建日志线程并发布可用状态
    const auto closeFile = [](FILE* file) { fclose(file); };
    unique_ptr<FILE, decltype(closeFile)> file(fopen(fileName.c_str(), "a"), closeFile);
    if (!file) {
        throw system_error(errno, generic_category(), "Cannot open log file " + fileName.string());
    }
    unique_ptr<BlockDeque<string>> queue;
    if (maxQueueCapacity > 0) {
        queue = make_unique<BlockDeque<string>>(static_cast<size_t>(maxQueueCapacity));
    }

    path_ = move(newPath);
    suffix_ = move(newSuffix);
    level_ = level;
    lineCnt_ = 0;
    toDay_ = calendar.tm_yday + calendar.tm_year * 366;
    buff_.RetrieveAll();
    deque_ = move(queue);
    fp_ = file.release();
    isAsync_ = maxQueueCapacity > 0;
    try {
        if (isAsync_) {
            writeThread_ = make_unique<thread>(FlushLogThread);
        }
    } catch (...) {
        fclose(fp_);
        fp_ = nullptr;
        deque_.reset();
        isAsync_ = false;
        throw;
    }
    isOpen_ = true;
}

Log* Log::Instance()
{
    static Log instance;
    return &instance;
}

void Log::FlushLogThread()
{
    Log::Instance()->AsyncWrite_();
}

void Log::write(int level, const char* format, ...)
{
    timeval now{};
    gettimeofday(&now, nullptr);
    const time_t seconds = now.tv_sec;
    tm calendar{};
    if (localtime_r(&seconds, &calendar) == nullptr) {
        return;
    }

    lock_guard<mutex> locker(mtx_);
    if (!isOpen_) {
        return;
    }
    const int day = calendar.tm_yday + calendar.tm_year * 366;
    if (toDay_ != day || (lineCnt_ > 0 && lineCnt_ % MAX_LINES == 0)) {
        char date[32]{};
        strftime(date, sizeof(date), "%Y_%m_%d", &calendar);
        const string tail = toDay_ != day ? "" : "-" + to_string(lineCnt_ / MAX_LINES);
        const auto fileName = filesystem::path(path_) / (string(date) + tail + suffix_);
        FILE* file = fopen(fileName.c_str(), "a");
        if (file) {
            fflush(fp_);
            fclose(fp_);
            fp_ = file;
            if (toDay_ != day) {
                toDay_ = day;
                lineCnt_ = 0;
            }
        } else {
            //轮转失败时保留原文件,不让写日志导致服务线程退出
            fprintf(stderr, "Cannot rotate log file: %s\n", strerror(errno));
        }
    }

    buff_.RetrieveAll();
    char timestamp[128]{};
    const int prefixLength = snprintf(timestamp, sizeof(timestamp),
        "%04d-%02d-%02d %02d:%02d:%02d.%06ld ",
        calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday,
        calendar.tm_hour, calendar.tm_min, calendar.tm_sec, now.tv_usec);
    buff_.Append(timestamp, static_cast<size_t>(prefixLength));
    AppendLogLevelTitle_(level);

    va_list arguments;
    va_start(arguments, format);
    va_list countArguments;
    va_copy(countArguments, arguments);
    const int length = vsnprintf(nullptr, 0, format, countArguments);
    va_end(countArguments);
    if (length < 0) {
        va_end(arguments);
        buff_.RetrieveAll();
        return;
    }
    try {
        //vsnprintf返回所需长度,必须先扩容再写入,不能把截断长度当作已写长度
        buff_.EnsureWritable(static_cast<size_t>(length) + 1);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, arguments);
    va_end(arguments);
    buff_.HasWritten(static_cast<size_t>(length));
    buff_.Append("\n", 1);
    ++lineCnt_;

    const string text = buff_.RetrieveAllToString();
    if (isAsync_ && !deque_->full()) {
        deque_->push_back(text);
    } else {
        fwrite(text.data(), 1, text.size(), fp_);
    }
}

void Log::flush()
{
    lock_guard<mutex> locker(mtx_);
    if (isAsync_) {
        deque_->flush();
    }
    if (fp_) {
        fflush(fp_);
    }
}

int Log::GetLevel()
{
    lock_guard<mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level)
{
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

bool Log::IsOpen()
{
    lock_guard<mutex> locker(mtx_);
    return isOpen_;
}

size_t Log::GetQueueCapacity()
{
    lock_guard<mutex> locker(mtx_);
    return deque_ ? deque_->capacity() : 0;
}
