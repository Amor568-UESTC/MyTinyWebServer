#include"buffer.h"

using namespace std;

void Buffer::MakeSpace_(size_t len)
{
    if(WritableBytes()+PrependableBytes()<len)
        buffer_.resize(writePos_+len+1);
    else 
    {
        size_t readable=ReadableBytes();
        copy(BeginPtr_()+readPos_,BeginPtr_()+writePos_,BeginPtr_());
        readPos_=0;
        writePos_=readPos_+readable;
        assert(readable==ReadableBytes());
    }
}

Buffer::Buffer(int initBufferSize): buffer_(initBufferSize),readPos_(0),writePos_(0) {}

void Buffer::EnsureWritable(size_t len)
{
    if(WritableBytes()<len)
        MakeSpace_(len);
    assert(WritableBytes()>=len);
}

void Buffer::Retrieve(size_t len)
{
    assert(len<=ReadableBytes());
    readPos_+=len;
}

void Buffer::RetrieveUntil(const char* end)
{
    assert(Peek()<=end);
    Retrieve(end-Peek());
}

void Buffer::RetrieveAll()
{
    bzero(&buffer_[0],buffer_.size());
    readPos_=0;
    writePos_=0;
}

string Buffer::RetrieveAllToString()
{
    string str(Peek(),ReadableBytes());
    RetrieveAll();
    return str;
}

void Buffer::Append(const char* str,size_t len)
{
    assert(str);
    EnsureWritable(len);
    copy(str,str+len,BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const void* data,size_t len)
{
    assert(data);
    Append(static_cast<const char*>(data),len);
}

void Buffer::Append(const std::string& str)
{
    Append(str.data(),str.length());
}

void Buffer::Append(const Buffer& buff)
{
    Append(buff.Peek(),buff.ReadableBytes());
}

ssize_t Buffer::ReadFd(int fd,int* Errno)
{
    char buff[65535];
    iovec iov[2];
    const size_t writable=WritableBytes();
    //分散读
    iov[0].iov_base=BeginPtr_()+writePos_;
    iov[0].iov_len=writable;
    iov[1].iov_base=buff;
    iov[1].iov_len=sizeof(buff);

    const ssize_t len=readv(fd,iov,2);
    if(len<0) *Errno=errno;
    else if(static_cast<size_t>(len)<=writable)
        writePos_+=len;
    else
    {
        writePos_=buffer_.size();
        Append(buff,len-writable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd,int* Errno)
{
    size_t readable=ReadableBytes();
    ssize_t len=write(fd,Peek(),readable);
    if(len<0)
    {
        *Errno=errno;
        return len;
    }
    readPos_+=len;
    return len;
}