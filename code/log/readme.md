### 实现阻塞队列

    //对于一个模板类，.cpp和.h文件分开会产生编译上的
    顺序错误，所以一般放在一起用.hpp作为后缀

## 类内部私有成员和函数
    # deq_           deque<T>
    stl中的双端对列，用于存储数据

    # capacity_      size_t 
    最大容量

    # mtx_           mutex
    锁机制，保证生产者和消费者操作互斥

    # isClose_       bool
    关闭标志

    # condConsumer_  condition_variable
    # condProducer_  condition_variable
    生产者和消费者的条件变量

    //生产者&消费者模式
    生产者为队列添加成员，消费者则是减少

## 类内部公有函数
    # BlockDeque(size_t )
    初始化设置最大容量
    explicit关键字禁止自动调用拷贝初始化，
    禁止拷贝函数对参数进行隐式转换

    # ~BlockDeque()
    调用Close()

    # clear()
    清空队列deq_

    # empty() full()
    判断deq_为空或满

    # Close()
    清空deq_并使得isClose_标志为1
    生产者和消费者唤醒全部线程

    # size()
    返回deq_的size

    # capacity()
    返回最大容量capacity_

    # back()
    返回deq_的back

    # push_back(const T& item)
    推入队列为生产者操作，先要判断是否满？
    若满则生产者等待，阻塞。
    推入后则消费者可以唤醒一个线程

    # push_front(const T& item)
    同上

    # pop(T& item)
    出队列为消费者操作，先判断空？
    若空消费者等待，注意关闭的情况！
    出队列，生产者可以唤醒一个线程。
    返回是否操作成功

    # pop(T& item,int timeout)
    同上，设置了消费者最大等待时间，超时失败

    # flush()
    冲刷，消费者唤醒一个线程

### log实现日志

    //单例模式：
    

## 类内部私有成员和函数

    # Log()
    构造函数，隐藏拒绝外部产生新的对象
    几个指针都置空

    # ~Log()
    先将队列中未出队的清空，然后关闭队列，写线程；


    # AppendLogLevelTitle_(int level)
    设置等级放入buff_中作为区分

    # AsyncWrite_()
    异步写，将队列中的数据写入fp_流中

    # path_         const char*
    文件路径名称

    # suffix_       const char*
    文件后缀名称

    # MAX_LINES     int
    单个文件中最大行数量

    # lineCnt_      int
    行数量

    # toDay_        int
    记录日期，为几号

    # level_        int 
    日志等级
    
    # isOpen_       bool
    是否开启标志

    # isAsync_      bool
    是否异步标志

    # buff_         Buffer
    缓冲池

    # fp_           FILE*
    封装的文件结构体，用于读写文件

    # deque_        unique_ptr<BlockDeque<string>>
    阻塞队列的独有指针，满足单例设计模式，
    队列中为要写入文件的数据(条)

    # writeThread_  unique_ptr<thread>
    写线程，单独一个用于写log文件，满足性能且占用资源少

    # mtx_          mutex
    🔓机制

## 类内部公有函数
    # init(int ,...)
    初始化level_,path_,suffix_等，设置isOpen_打开，
    若队列capacity设置大于0,即isAsync_打开，否则队列
    不打开，即为同步。
    随后设置文件名称为日期，清空buff_数据，若fp_存在则
    先冲刷清空，关闭，最后重新打开，否则创建并打开。

    # Instance()
    返回单个实例的地址

    # FlushLogThread()
    冲刷日志线程，即调用实例中的异步写函数

    # write(int level,const char* format,...)
    先查看日期是否正确或者行数是否超出?若是，则使用新的文件，
    更新文件名称。
    然后锁上，进行数据转移：队列-->fp_-->磁盘文件。
    

    # flush()
    若为异步则冲刷队列deque_，再冲刷fp_,将fp_中
    数据全部写入磁盘文件

    # GetLevel()
    获取level_

    # SetLevel(int level)
    设置level_

    # IsOpen()
    查看isOpen_的值
