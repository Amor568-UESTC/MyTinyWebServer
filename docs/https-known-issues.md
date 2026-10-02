# HTTPS 已知问题与后续修复计划

记录日期：2026-10-03。审查分支：`feature/https`，以当时完整工作区为准。

HTTPS 的证书加载、握手、加密读写和页面访问主流程已经接通。开发者已用 `curl --cacert` 和内网手机验证访问成功；审查时完整工作区也能编译通过。但下面的问题仍然存在，不能据此认定服务器已经稳定。本次只新增忽略规则并记录待办，没有修复这些问题。

建议先在 feature 分支保存阶段性提交。如果 `master` 代表稳定可用版本，建议修复空文件崩溃、分段请求处理，以及 HTTPS 关闭通知导致请求丢失的问题后，再评估合入。HTTP 的既有缺陷不应算作 HTTPS 新引入的回归；其中共用请求解析和响应代码的问题，也会影响 HTTPS 服务。

## 已完成的验证与验证范围

| 验证 | 结果与范围 |
| --- | --- |
| 完整工作区编译 | 通过，使用 C++23，链接 pthread、mysqlclient、ssl、crypto；不代表当前暂存区能单独编译 |
| 本机 HTTPS 页面访问 | 开发者使用 `curl --cacert` 成功获取 HTML |
| 内网手机访问 | 开发者手动验证成功 |
| 最小复现程序 | 已复现下列空文件崩溃、映射泄漏、解析异常、HTTP 部分发送和 TLS 关闭通知问题 |
| 自动化回归 | 尚未形成覆盖以下边界情况、可在仓库中重复运行的测试集 |

最小复现程序在临时目录运行，没有加入仓库。下文保留了输入、观测结果和验收要求，后续修复时应将它们整理为正式测试。这份清单不代表一次完整的安全审计或压力测试。

## 待修复问题

这里的 P1 表示应优先处理的崩溃或基本请求正确性问题；P2 表示资源或特定边界问题。优先级是本项目后续开发的建议顺序。

### 1 空文件或 mmap 失败会导致进程崩溃

**P1，共用 HTTP 响应代码的既有问题，HTTP 和 HTTPS 都受影响。**

位置：[httpresponse.cpp](../code/http/httpresponse.cpp)，`HttpResponse::AddContent_()`。

当前代码用 `if (*mmRet == -1)` 判断 `mmap()` 是否失败。这会解引用返回的地址；映射失败时应比较返回值和 `MAP_FAILED`。空文件的映射长度为 0，会进入失败场景。

已复现：为一个可读的空文件构造响应，测试进程收到 `SIGSEGV`，Python 子进程接口记录的返回值为 `-11`。普通非空文件对照测试可以完成。

修复方向：

- 先处理文件长度为 0 的情况，返回 `Content-Length: 0`，不调用 `mmap()`。
- 用返回值与 `MAP_FAILED` 比较，避免解引用失败指针。
- 无论映射成功还是失败，都要关闭打开的文件描述符；失败时不要留下可供发送的无效文件指针和长度。

验收：空文件返回完整的空响应，普通文件内容正确；模拟映射失败时返回可解析的错误响应，进程存活且无文件描述符泄漏。崩溃复现应在独立测试进程中执行。

### 2 分段请求和连续请求解析不完整

**P1，共用 HTTP 解析逻辑的既有问题，HTTP 和 HTTPS 都受影响。**

位置：[httprequest.cpp](../code/http/httprequest.cpp) 的 `HttpRequest::parse()`，以及 [httpconn.cpp](../code/http/httpconn.cpp) 的 `HttpConn::process()`。

`parse()` 只有成功或失败两种结果，无法表示“格式暂时合法，但数据还没收齐”。未找到完整 CRLF 时也会尝试处理当前数据；函数末尾并不要求解析状态到达 `FINISH`。同时，`process()` 每次调用都会重置请求状态，请求体也没有按 `Content-Length` 等待完整数据。

已复现的输入与结果：

| 输入方式 | 当前结果 |
| --- | --- |
| 第一批只传入 `GET /` | 返回解析失败，上层会准备 400 响应，无法等待剩余请求行 |
| 完整请求行后只传入部分 Host 头 | 在请求头尚未结束时返回成功 |
| POST 声明 `Content-Length: 14`，当前只收到 12 字节的 `username=ali` | 提前返回成功，并解析出尚未收完整的表单 |
| 两个完整的 Keep-Alive GET 一起放入缓冲区 | 第二个请求行被当作前一个请求的请求体消耗，第二次解析失败 |

修复方向：

- 定义 `NeedMore`、`Complete`、`BadRequest` 三种解析结果；只有 `Complete` 才生成正常响应。
- 解析到完整 CRLF 后再消费请求行和头字段；数据不足时保留输入和解析状态。
- 头部结束后根据请求体长度决定何时完成。校验 `Content-Length`，对尚未支持的消息定界方式明确处理，避免默默误读。
- 完成当前请求后才重置解析状态，缓冲区中属于下一个请求的字节必须保留。

验收：分别在请求行、头字段、头部结束标记和请求体中间拆分发送；服务器应等待并最终只处理一次请求。两个连续请求应分别得到响应，非法请求仍应被拒绝。测试应同时覆盖 HTTP 和 HTTPS，因为 TLS 记录边界不等于 HTTP 请求边界。

### 3 Keep-Alive 重复响应会泄漏文件映射

**P2，共用 HTTP 响应代码的既有问题。**

位置：[httpresponse.cpp](../code/http/httpresponse.cpp)，`HttpResponse::Init()`。

初始化下一次响应时，代码直接将 `mmFile_` 置空、将 `mmFileStat_` 清零，没有先释放上一次映射。旧映射地址和长度丢失后，析构或关闭连接也无法再释放它。

已复现：同一个 `HttpResponse` 连续创建 10 次文件响应，可观察到 10 个对应文件的映射；最后调用一次 `UnmapFile()` 后仍残留 9 个。单次响应对照测试的映射可以正常释放。

修复方向：在旧响应发送完成后、覆盖旧地址和长度前调用 `UnmapFile()`，再初始化新响应。不要在发送仍引用旧映射时释放它。

验收：在同一 Keep-Alive 连接上连续请求文件，观察 `/proc/<pid>/maps` 中对应文件的映射数量，不能随请求次数持续增长；关闭连接后应释放最后一次映射。

### 4 普通 HTTP 部分发送后可能提前关闭连接

**P2，HTTP 既有问题；目前补偿逻辑只覆盖了 HTTPS。**

位置：[webserver.cpp](../code/server/webserver.cpp) 的 `WebServer::OnWrite_()`，以及 [httpconn.cpp](../code/http/httpconn.cpp) 的 `HttpConn::write()`。

`write()` 可能先写出部分数据，再遇到 `EAGAIN`，最终返回累计写出的正数字节数。当前 HTTP 分支只在返回值为负且错误为 `EAGAIN` 时继续等待，正返回但仍有待发送数据时会落到关闭连接。已有的 `ToWriteBytes() > 0` 补偿判断放在 `if (https)` 内，普通 HTTP 用不到。

已用非阻塞 socketpair 复现：准备发送 4 MiB，`write()` 返回 `219264`，仍剩 `3975040` 字节，错误码为 `EAGAIN`。按当前 HTTP 分支会提前关闭连接。

修复方向：完成 TLS 专属的错误与挂起操作处理后，在 HTTP 和 HTTPS 共用的路径中检查剩余数据；可继续发送时重新监听 `EPOLLOUT`。仍须保留 TLS `WANT_READ`、`WANT_WRITE` 对实际等待方向的要求。

验收：用小发送缓冲区和慢速接收端传输较大响应，在 ET 与 LT 模式下分别检查接收长度和内容；部分发送后不能提前断开。

### 5 TLS 关闭通知可能导致已读请求被丢弃

**P2，HTTPS 新路径中的边界问题。**

位置：[httpsconn.cpp](../code/http/httpsconn.cpp) 的 `HttpsConn::read()`，以及 [webserver.cpp](../code/server/webserver.cpp) 的 `WebServer::OnRead_()`。

ET 循环可能先读到完整请求，随后又读到客户端的 `close_notify`。此时 `read()` 返回已读取的正数字节数，但状态为 `Closed`；`OnRead_()` 看到 `Closed` 就直接关闭，没有处理已经收到的请求。

已通过 socketpair 建立真实 TLS 1.3 会话，并直接调用 `HttpsConn::read()` 复现上述状态组合：客户端发送 54 字节的完整 GET 后调用一次 `SSL_shutdown()`，服务端读取返回 `54`，状态同时为 `Closed`，错误为 `SSL_ERROR_ZERO_RETURN`。按当前 `OnRead_()` 源码，该请求随后会被丢弃；本项没有运行整个服务器的端到端复现。

TLS 1.3 允许发送关闭通知的一方继续接收对端数据，因此“对端不再发送”不能直接等同于“本端不能再响应”。参见 [OpenSSL SSL_shutdown 文档](https://docs.openssl.org/3.0/man3/SSL_shutdown/)。

修复方向：记录读方向已关闭；若已收到完整请求，继续生成和发送响应，完成后关闭连接。若仅有不完整请求，则按明确策略终止。不要把致命 TLS 错误也当作可以继续处理的正常关闭。

验收：客户端发送完整请求及 `close_notify` 后继续读取，服务端应返回完整响应并结束连接；只发送部分请求就关闭时，不能误生成正常响应，也不能无限等待。

### 6 关闭连接与工作线程的生命周期协调

**P1，共用服务器的既有并发风险，按当前开发决定暂缓处理。**

位置：[webserver.cpp](../code/server/webserver.cpp) 中的任务投递、超时回调、连接关闭与 fd 复用路径，以及 [httpconn.cpp](../code/http/httpconn.cpp)、[httpsconn.cpp](../code/http/httpsconn.cpp) 中的资源释放。

主线程可能因超时或错误关闭连接，而工作线程仍在读写、解析或访问文件映射。HTTPS 还增加了正在使用 `SSL*` 时被另一条执行路径释放的风险。投递任务使用的裸指针、旧定时器和复用的 fd 也需要统一考虑。

本项来自代码审查，未在本次通过并发压力测试复现；不能将暂缓处理记为已解决。

后续设计方向：明确连接与资源的所有者，记录在途任务和关闭请求；工作线程完成后通知负责 epoll 的线程，由它协调重新注册事件和最终释放。任务、定时器与连接代次关联，避免 fd 复用时误操作新连接。服务器退出时也要等待任务结束再销毁依赖资源。

验收：覆盖慢速请求期间超时、对端中断、反复创建关闭连接导致 fd 复用，以及服务器带在途任务退出；结合 ASan、TSan 和压力测试检查重复释放、悬空访问及数据竞争。

## 其他后续完善项

- **大文件长度。** [httpsconn.cpp](../code/http/httpsconn.cpp) 将 `iov_len` 传给接收 `int` 长度的 `SSL_write()`，而 `ToWriteBytes()` 也返回 `int`。超过 `INT_MAX` 的长度存在截断或范围问题；后续统一长度类型并限制单次发送大小。本次未做超大文件验证。
- **测试与说明同步。** 现有 [HTTPS 测试说明](../code/http/test/readme.md) 和测试程序需要随 `InitSSL()` 等接口更新，补上真实握手和上述回归场景；现有文件不等同于完整的 HTTPS 验收套件。
- **差异中的尾随空白。** 本次审查时 `git diff HEAD --check` 在 `iobuffer.cpp`、`httpsconn.cpp`、`webserver.cpp`、`heaptimer.cpp` 报告尾随空白。后续提交前顺手整理。

## 提交整理

已新增的 [.gitignore](../.gitignore) 忽略本地 `ssl/`、`bin/`、日志、编译缓存、私钥类文件和编辑器临时文件。手写的 `build/Makefile`、源码、共享 VS Code 配置与 `rss/` 站点资源继续保留。

`ssl/` 中的开发证书和私钥需要在每台开发机器上自行准备。公开证书本身不是私钥；这里忽略整个目录，是为了避免提交本机签发材料和机器相关配置。`.gitignore` 也不会清除已经跟踪或历史提交中的文件。

当前还有以下 8 个文件已经被 Git 跟踪，新规则不会自动取消跟踪：

- `code/http/test/httpsconnTest`
- `code/pool/test/testThreadPool`
- `webbench-1.5/webbench`
- `webbench-1.5/webbench.o`
- `rss/css/.DS_Store`
- `rss/fonts/.DS_Store`
- `rss/images/.DS_Store`
- `rss/js/.DS_Store`

如要将它们从后续版本中移除，同时保留本地文件，可在整理提交时执行以下命令。本次没有执行，也没有修改暂存区。

```bash
git rm --cached -- \
  code/http/test/httpsconnTest \
  code/pool/test/testThreadPool \
  webbench-1.5/webbench \
  webbench-1.5/webbench.o \
  rss/css/.DS_Store \
  rss/fonts/.DS_Store \
  rss/images/.DS_Store \
  rss/js/.DS_Store
```

审查时暂存区只有 5 个已修改文件，完整的 HTTPS 修改尚未全部暂存。单独导出的暂存区版本编译失败，不能把“工作区编译通过”当成“马上 commit 就会得到可编译版本”。

提交前检查：

- [ ] 有意识地暂存本次相关源码、构建文件、忽略规则和文档，检查 `git diff --cached`。
- [ ] 确认证书私钥、运行日志、编译产物不在本次提交中。
- [ ] 整理已跟踪的生成物和尾随空白。
- [ ] 对最终准备提交的文件集合重新构建，避免遗漏接口或链接参数修改。

## 完成标准与合入建议

当前可以描述为 **HTTPS 基础功能已接入，存在已知问题**，适合保存一次阶段性提交。不能将当前状态描述为“HTTPS 所有边界情况已完成”或“服务器已达到稳定版本”。

建议下次先修复问题 1 和 2，再处理问题 5，随后补上问题 3 和 4 的回归测试并修复。问题 6 按当前决定单独安排，但仍是并发可靠性的欠账；推迟它不代表这些场景已经通过验证。

如果 `master` 用来保留稳定版本，目前建议先留在 `feature/https`。如果它只是个人学习进度的汇总分支，可以按“有已知问题的阶段性功能”合入，但应保留这份清单，确保最终提交能独立构建，并避免把合入等同于稳定性验收通过。本次未执行 commit 或 merge。
