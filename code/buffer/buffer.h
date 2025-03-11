#pragma once 

#include<cstring>
#include<iostream>
#include<unistd.h>
#include<sys/uio.h>
#include<vector>
#include<atomic>
#include<assert.h>

class Buffer
{
private:
    const char* BeginPtr_() const {return &*buffer_.begin();}
    char* BeginPtr_() {return &*buffer_.begin();}
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::atomic<std::size_t> readPos_;
    std::atomic<std::size_t> writePos_;

public:
    Buffer(int initBufferSize=1024);
    ~Buffer()=default;

    size_t WritableBytes() const {return buffer_.size()-writePos_;}
    size_t ReadableBytes() const {return writePos_-readPos_;}
    size_t PrependableBytes() const {return readPos_;}

    const char* Peek() const {return BeginPtr_()+readPos_;}
    void EnsureWritable(size_t len);
    void HasWritten(size_t len) {writePos_+=len;}

    void Retrieve(size_t len);
    void RetrieveUntil(const char* end);
    void RetrieveAll();
    std::string RetrieveAllToString();

    const char* BeginWriteConst() const {return BeginPtr_()+writePos_;}
    char* BeginWrite() {return BeginPtr_()+writePos_;}

    void Append(const char* str,size_t len);
    void Append(const void* data,size_t len);
    void Append(const std::string& str);
    void Append(const Buffer& buff);

    ssize_t ReadFd(int fd,int* Errno);
    ssize_t WriteFd(int fd,int* Errno);
};