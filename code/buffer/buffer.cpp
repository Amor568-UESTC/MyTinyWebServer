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
