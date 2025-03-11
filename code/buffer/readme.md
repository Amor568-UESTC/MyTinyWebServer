### 自实现的buffer

## 类内部私有成员和函数
    # buffer_     (vector<char>类型)
    用于存放缓冲区中的内容

    # readPos_    (atomic<size_t>类型) 
    读位置

    //为何要使用原子操作的读写位置？
    因为在多线程环境中，一个Buffer类对象往往要被多个
    线程操作，原子操作保障操作的顺序性，避免数据竞争。
    同时不使用锁，提升性能，保障简洁性。

    # writePos_   (atomic<size_t>类型)
    写位置

    # MakeSpace_(size_t len)
    若可写+可前置<len,空间不足，buffer_ resize
    空间足够，覆盖readPos_前的所有数据，节约空间

## 类内部公有函数
    # Buffer(int )  构造函数
    构造初始化buffer_大小，并让两个Pos_为0

    # 可写，可读，可前置字节数函数

    # Peek() 返回readPos_的指针

    # EnsureWritable(size_t len)
    确保能写len个长度，不够则MakeSpace_

    # Retrieve(size_t len)
    即读取len长度数据

    # RetrieveUntil(const char* end)
    读取到end指针处

    # RetrieveAll()
    全部读完，把buffer_全部数据置为0,重新设置Pos_

    # RetrieveAllToString()
    把可读数据保存到str中，清空数据，返回str

    # Append(const char* str,size_t len)
    即向buffer_内写入长度为len，str

    # Append(const void* data,size_t len)
    # Append(const string& str)
    同上

    # Append(const Buffer& buff)
    将同类对象的readPos_之后的数据（即可读数据）
    拷贝进本对象的buffer_中

    # ReadFd(int fd,int* Errno)
    从fd中读取数据，使用额外的普通缓冲区，防止数据太大
    若数据长度len<writable,则只要writePos_+=len,
    若大于，再把buff中数据append

    # WriteFd(int fd,int* Errno)
    向fd中写数据，很好理解

    
