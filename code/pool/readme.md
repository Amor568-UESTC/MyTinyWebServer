### 池结构

## 成员

    # mtx       mutex
    🔓机制

    # cond      condition_varialble
    条件变量

    # isClosed  bool
    关闭标志

    # tasks     queue<function<void()>>
    任务队列


### 简单线程池的实现

## 类内部私有成员和函数

    # pool_  shared_ptr<Pool>
    共享指针，共享了本池

## 类内部公有函数

    # ThreadPool(size_t threadCnt)
    explicit关键字禁止自动调用拷贝初始化，
    禁止拷贝函数对参数进行隐式转换
    初始化pool_,设置线程数目为threadCnt，
    调用线程detach()，一直在后台运作。
    若队列不空，取出队列头执行任务；若为空，
    考虑关闭情况，不关闭则等待。

    # ThreadPool() / ThreadPool(ThreadPool&&)
    默认构造

    # ~ThreadPool()
    将pool_转换为bool类型判断是否为空，若为空
    则可以析构，将isClose_设置为1即可，再唤醒
    全部线程。shared_point会自己释放资源。

    # AddTask(F&& task)
    为任务队列添加任务，完美转发



### sql连接池的实现

    //单例设计模式,同样为懒汉模式

## 类内部私有成员和函数

    # SqlConnPool()
    简单初始化useCnt_和freeCnt_

    # ~SqlConnPool()
    调用ClosePool()

    # MAX_CONN_     int 
    最大连接数目，等于下两个成员之和

    # useCnt_       int 
    正在使用的连接数目

    # freeCnt_      int
    未使用的连接数目

    # connQue_      queue<MYSQL*>
    MYSQL*连接句柄的队列，里面会放置空闲的连接句柄

    # mtx_          mutex
    🔓机制

    # semId_        sem_t
    信号量数据，对此信号量做加减


## 类内部公有函数

    # Instance()
    构造SqlConnPool实例并返回地址

    # GetConn()
    先判断连接队列是否为空，若空表示所有连接都在
    进行操作，发出WARN；否则信号量-1,从队列中取得
    头并出队列，返回该句柄

    # FreeConn(MYSQL* conn)
    将conn句柄放入connQue_队列末尾，信号量+1

    # GetFreeConnCnt()
    得到空闲的句柄数目，即队列大小

    # Init(const char* host ...)
    初始化操作，从connSize中得到连接总数，其他参数中
    得到网络地址，数据库信息并尝试连接。连接成功则放入
    connQue_队列中待命，将信号量初始化为最大连接数量

    # ClosePool()
    将队列中的连接句柄全部释放，然后释放mysql库的资源



### sqlConnRAII

    //RAII：资源获取即初始化
    此类作用为获取SqlConnPool中队列头部的sql，
    调用完成操作后又马上放到队尾

## 类内部私有成员和函数

    # sql_          MYSQL*
    使用的sql_句柄

    # connpool_     SqlConnPool*
    与句柄相关的连接池

## 类内部公有函数

    # SqlConnRAII(MYSQL** sql,SqlConnPool* connpool)
    调用connpool的GetConn函数得到其头，给到*sql，
    并初始化内部成员

    # ~SqlConnRAII()
    调用connpool_的FreeConn函数将sql_放入队列尾部