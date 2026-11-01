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



### SQL 连接池

登录注册调用点已移到 [UserService](../handler/userservice.cpp),HTTP 解析器不再借用数据库连接.业务层使用参数化查询,连接池的借还和生命周期规则保持如下约定.

更新日期：2026-10-24。实现见 [sqlconnpool.h](sqlconnpool.h)、[sqlconnpool.cpp](sqlconnpool.cpp) 和 [sqlconnRAII.h](sqlconnRAII.h)。

当前采用“池空时返回失败”的策略：取得互斥锁后检查空闲队列，没有连接就返回 `nullptr`，不等待其他请求归还连接。调用者执行 SQL 时不持有连接池的锁，因此不同连接可以并行查询。

| 成员 | 含义 |
| --- | --- |
| `initialized_` | 所有连接成功建立并发布后设为 true；失败时保持 false，可以重新尝试初始化 |
| `closed_` | 关闭后禁止新借出和重新初始化；已经借出的连接归还时直接关闭 |
| `connQue_` | 当前空闲的连接句柄，已借出的句柄不在队列中 |
| `mtx_` | 保护池状态与队列的检查、取出、归还和交换 |

#### 信号量和互斥锁的分工

原实现同时使用了信号量和互斥锁：信号量表示可借的连接数量，互斥锁保护共享队列。计数信号量可以允许多个线程同时取得许可，所以不能单独保证 `std::queue` 的并发安全。

删除信号量不是修复所必需的条件。当前不需要等待连接归还，直接在同一个临界区检查并取出连接即可，避免维护队列和信号量两份状态。原先在锁外检查队列、在循环里重新初始化信号量的写法需要修正。

如果以后需要“池空时等待连接”，可以使用信号量配合互斥锁，或者使用条件变量配合互斥锁，并一起设计等待超时及关闭时唤醒等待者。

#### 初始化与失败处理

主线程首次调用 `Instance()` 时完成 MySQL 客户端库的进程级初始化；`WebServer` 在数据库连接池初始化成功后才创建工作线程。

`Init()` 用临时 `unique_ptr<MYSQL, decltype(&mysql_close)>` 持有每个新句柄。任意一步失败时，当前句柄和此前已经成功连接的句柄都自动关闭；全部成功后才构造空闲队列并交给池持有。成功后拒绝重复初始化。

`mysql_real_connect()` 的返回值只用于检查，不覆盖最初的句柄。失败时先从原句柄取得错误码和消息，再由资源所有者关闭。异常传到现有 `main()`，通过 stderr 输出并返回非零退出码，不在半初始化状态继续服务。

#### 借出、归还与 RAII

`GetConn()` 将状态检查、队列检查和取出放在同一个临界区；`FreeConn()` 在锁内归还，空指针不入队。每次成功借出只能归还一次，不能返回不属于本池的句柄。

调用点必须使用具名的 RAII 对象：

```cpp
MYSQL* sql = nullptr;
SqlConnRAII sqlGuard(&sql, SqlConnPool::Instance());
if (sql == nullptr) {
    LOG_ERROR("No database connection available");
    return false;
}
```

`sqlGuard` 存活期间独占借出的连接，离开作用域时归还；正常返回、提前返回都由同一个析构路径处理。不能再在函数末尾手动调用 `FreeConn(sql)`。RAII 类禁止复制和移动，防止多个对象归还同一次借出的连接。

RAII 内的 `thread_local` 状态为每个使用 MySQL 的线程初始化客户端线程状态，在线程退出时清理；该状态跟随线程，不跟随每一次连接借还。线程初始化失败时不会借出连接，调用点按无连接处理。

#### 关闭与生命周期边界

`ClosePool()` 设置关闭状态并取出空闲队列，随后关闭这些空闲连接；重复调用不重复关闭句柄。已经借出的连接保持有效，归还时由 `FreeConn()` 关闭。

关闭池与销毁池是两件事。池对象销毁前，所有使用者必须已经结束并归还连接。现有线程池使用 detach，完整的等待退出和连接/SSL 资源协调仍按 [HTTPS 问题 6](../../docs/https-known-issues.md) 单独处理。

`mysql_library_end()` 是进程级清理，应在所有 MySQL 使用线程退出、线程状态清理完成、连接关闭后统一执行一次；当前不在可被重复调用的 `ClosePool()` 中执行，最终退出顺序仍待完善。

#### 仓库内回归

在本模块的 test 目录执行:

```bash
cd code/pool/test
make test
```

已经位于该目录时只需 `make test`.单独 `make` 编译出 `./sqlconnpoolTest`,`make clean` 清理它.根目录的 `make sql-test` 保留为转发入口.独立构建规则和旧 ThreadPoolV2 测试的编译方式见 [测试说明](test/readme.md).

[sqlconnpoolTest.cpp](test/sqlconnpoolTest.cpp) 通过链接器包装模拟 MySQL API，不需要真实数据库、账号或证书。每个场景在独立进程中运行，避免单例状态相互影响；失败返回非零退出码，Makefile 给每个场景设置 10 秒超时。

当前 15 个场景覆盖库初始化失败、未初始化池、句柄初始化失败、部分连接失败与重试、借还和池耗尽、RAII 存活期与提前返回、关闭后归还、重复初始化、并发借出独占、线程状态、线程初始化失败、无连接/池耗尽时登录失败，以及非法池大小。

并发检查使用 12 个线程借还 3 条模拟连接，同时检查句柄不会被两个线程占用。它验证队列与借还协议，不替代真实 SQL 查询、账号权限、完整登录注册以及正常停机的验收。
