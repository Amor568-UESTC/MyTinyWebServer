# 服务器模块

更新日期：2026-10-24。`main()` 通过 `LoadConfig()` 读取并校验 TOML，再把拥有字符串存储的 `ServerConfig` 移入 `WebServer`。具体字段、启动命令与测试入口见 [配置化指南](../../docs/config-and-hot-reload-guide.md)。

端口、ET/LT 模式、超时、资源路径、线程数、数据库和 TLS 参数已接入。数据库初始化失败上报、日志初始化顺序、日志阈值和队列容量的实际使用仍需按指南阶段四完善；连接与工作线程的关闭协调继续暂缓。

### 将epoll封装为Epoller类

## 类内部私有成员和函数

    # epollFd_      int
    epoll文件描述符

    # events_       vector<epoll_event>
    事件结构体数组

## 类内部公有函数

    # Epoller(int maxEvent)
    explicit禁止隐式构造...
    初始化events_数组，调用epoll_create()初始化
    红黑树，消息队列资源等，返回给epollFd_描述符

    # ~Epoller()
    调用close(epollFd_)，关闭并释放资源

    # AddFd(int fd,uint32_t events)
    创建fd和events的epoll_event结构并放入
    红黑树中。unit32_t类型为epoll_event::
    events的类型，描述了事件类型。返回添加是否
    成功。

    # ModFd(int fd,uint32_t events)
    修改红黑树中fd的events属性，返回是否成功

    # DelFd(int fd)
    删除红黑树中fd描述符的epoll_event

    # Wait(int timeoutMS)
    调用epoll_wait,IO复用监听红黑树，timeoutMS
    设置阻塞时间，-1为一直阻塞

    # GetEventFd(size_t i) const
    返回数组events_下标为i的fd值

    # GetEvents(size_t i) const
    返回数组下标为i的events值


### 程序核心，最大类WebServer

    //了解该类可以宏观上查看本程序调用各个方法的
    顺序，了解运作基本原理

## 类内部私有成员和函数

    # initSocket_()
    套接字初始化函数。初始化Ip地址端口等，初始化optLinger，
    若config_.server.linger为true,表示linger开启，控制socket关闭时间
    为1s，否则默认会直到缓存中数据发送完毕才关闭socket。
    初始化listenFd_，设置listenFd_的LINGER，设置为多路复用
    模式，绑定ListenFd_的地址。将listenFd_加入epoll红黑树
    中，设置为非阻塞模式

    # InitEventMode_(TriggerMode listenMode, TriggerMode connectionMode)
    根据监听和连接各自的触发模式，设置listenEvent_和connEvent_
    是否为水平或者边缘触发。先将listenEvent_设置为
    EPOLLRDHUP若对端关闭写方向则会触发，connEvent_多设置一个
    EPOLLONESHOT事件被处理后，epoll不会自动重新注册该事件。
    这意味着，当事件被触发并被处理后，如果需要再次监听该事件，
    必须手动重新注册。在多线程环境中，它可以确保一个事件只
    被一个线程处理一次，避免竞态条件。最后将HttpConn::isEt
    根据connEvent_为EPOLLET设置

    # AddClient_(int fd,sockaddr_in addr)
    添加客户端。先使用函数参数初始化user_[fd]，如果config_.server.idleTimeoutMs
    设置>0，则向timer_中添加绑定关闭客户端函数。设置fd非阻塞

    # DealListen_()
    解决监听事件函数。调用accept接受给fd，判断是否超出最大连接
    若超出则警告并返回，否则AddClient。循环条件为listenEvent_
    为EPOLLET

    # DealWrite_(HttpConn* client)
    解决写事件函数。先更新client的过期时间。向线程池中添加
    这个客户端绑定OnWrite_函数

    # DealRead_(HttpConn* client)
    解决读事件函数。同上

    # SendError_(int fd,const char* info)
    发送错误，向fd发送错误并关闭fd

    # ExtentTime_(HttpConn* client)
    更新timer_中client对应fd的过期时间

    # CloseConn(HttpConn* client)
    将client从红黑树中取出，并调用其Close函数

    # OnRead_(HttpConn* client)
    调用client的read函数，读完则关闭client，调用
    OnProcess()函数

    # OnWrite_(HttpConn* client)
    调用client的write函数，若已经全部写完且仍保持存活，
    调用OnProcess函数，若为EAGAIN错误，表示没有写完，
    继续让client为写模式。最终关闭客户端

    # OnProcess_(HttpConn* client)
    调用client的porcess函数，成功则开始写，否则继续读。
    即判断client此时应当读或者写，进行转换

    # MAX_FD        static const int
    最大FD数量

    # SetFdNonblock(int fd)
    设置fd为非阻塞

    # config_           const ServerConfig
    服务器持有的启动配置。端口、linger、空闲超时、HTTPS开关等
    直接读取config_，不再重复保存成独立配置成员。

    # isClose_          bool
    是否关闭标志

    # listenFd_         int
    监听描述符

    # listenEvent_      uint32_t
    描述listenFd_性质

    # connEvent_        uint32_t
    描述客户端连接事件性质

    # timer_            unique_ptr<HeapTimer>
    定时器

    # threadpool_       unique_ptr<ThreadPool>
    线程池

    # epoller_          unique_ptr<Epoller>
    epoll使用类

    # user_             unordered_map<int,HttpConn>
    用户

## 类内部公有函数

    # WebServer(ServerConfig config)
    按值接收配置并移入config_，初始化运行状态、定时器、线程池
    和epoll。HttpConn::srcDir借用config_.server.documentRoot的
    字符串存储，配置在服务器运行期间保持不变。初始化连接池实例，
    初始化事件模式，套接字等。若开启Log，初始化Log实例，向Log中
    写入基本信息

    # ~WebServer()
    关闭listenFd_,isClose_标志置1，关闭sql连接池
    实例。

    # void Start()
    开始服务器运行。设置timeMS为-1，进入循环。若config_.server.idleTimeoutMs
    >0，设置timeMS为定时器下个事件过期时间，调用epoller检测IO。
    检测完成后根据就绪IO的事件类型进行操作：监听，读，或者写事件。


