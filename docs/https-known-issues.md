# HTTPS 已知问题与后续修复计划

首次记录：2026-10-03。最近复核：2026-10-19。分支：`feature/https`，以完整工作区为准。

HTTPS 的证书加载、握手、加密读写和页面访问主流程已经接通。原问题 1～5 已修复，问题 6 按开发决定暂缓。大文件长度处理也已补齐。当前适合保存阶段性修复提交，尚不能视为所有边界和并发场景均已完成验收。

## 状态总览

保留首次记录时的问题编号，便于对应历史讨论。

| 编号 | 问题 | 当前状态 |
| --- | --- | --- |
| 1 | 空文件或 mmap 失败导致崩溃 | 已修复，映射失败返回 500，清空指针和长度并关闭 fd |
| 2 | 分段请求和连续请求解析不完整 | 已修复，解析与相关 TLS 回归通过 |
| 3 | Keep-Alive 重复响应泄漏文件映射 | 已修复，重复映射与释放检查通过 |
| 4 | 普通 HTTP 部分发送后提前关闭 | 已修复，ET/LT 部分发送回归通过 |
| 5 | TLS 关闭通知导致已读请求丢失 | 已修复，关闭通知与 TCP FIN 组合场景通过 |
| 6 | 关闭连接与工作线程生命周期协调 | 暂缓，未实现并发协调，也未完成并发验收 |

## 已完成的验证与范围

| 验证 | 结果与范围 |
| --- | --- |
| 完整工作区编译 | 通过；C++23，链接 pthread、mysqlclient、ssl、crypto |
| 本机 HTTPS 页面访问 | 开发者此前使用 `curl --cacert` 成功获取 HTML |
| 内网手机访问 | 开发者此前手动验证成功 |
| 请求解析 | 合法/非法头字段、Content-Length、未支持的 Transfer-Encoding、分段输入、连续请求和 Connection 选项回归通过 |
| 空文件与映射失败 | 仓库内 9 项文件响应回归通过；空文件正常返回，模拟 mmap 失败返回 500、指针和长度清零、fd 数量保持稳定 |
| 重复文件响应 | 同一响应对象生成 10 次文件响应，仅保留 1 个映射；显式释放后为 0 |
| HTTP 部分发送 | ET/LT 下，8 KiB 和 4 MiB 响应均完整接收，部分发送后未提前关闭 |
| TLS 边界 | 基于真实 TLS 1.3 会话的 14 个场景通过，覆盖关闭通知、分段请求、连续请求、挂起写恢复、异常 EOF、空错误指针及握手失败状态 |
| 大文件长度 | 模拟 2 GiB 待发送长度未截断；两处 TLS 写入入口的单次参数均限制为最多 16 KiB |

文件响应回归已整理进 [httpresponseTest.cpp](../code/http/test/httpresponseTest.cpp)，编译和运行方式见 [测试说明](../code/http/test/readme.md)。其余回归程序仍在临时目录运行，尚未加入仓库。TLS 和 HTTP 回调测试使用单线程驱动调用实际连接代码及从源码提取的事件处理逻辑，没有启动完整的 SQL/线程池服务。2 GiB 检查验证长度和传参，不代表实际传输了 2 GiB 文件。

尚未完成并发压力、ASan/TSan、完整服务端端到端边界测试，以及全部登录/注册业务验证。仓库旧测试程序的限制见 [HTTPS 测试说明](../code/http/test/readme.md)。

## 原问题的修复记录与剩余工作

### 1 空文件或 mmap 失败导致崩溃：已修复

位置：[httpresponse.cpp](../code/http/httpresponse.cpp)，`HttpResponse::MapFile_()` 与 `MakeResponse()`。

已完成：

- 空文件返回 `Content-Length: 0`，不调用零长度 `mmap()`。
- 比较返回值与 `MAP_FAILED`，不再解引用失败地址。
- 成功、空文件和映射失败路径均关闭打开的文件描述符。
- `MapFile_()` 在失败时清空文件指针和长度，并将状态设为 500；`CODE_STATUS` 包含对应的 Internal Server Error。
- `MakeResponse()` 先确定映射结果，再输出状态行、响应头和文件长度或内联错误正文。
- 内联错误正文使用 HTML 类型，避免图片请求失败时仍声明为图片内容。

仓库内回归覆盖普通文件、空文件、映射失败、图片映射失败时的错误类型、失败后的下一次响应、文件形式的 404、内联 404 回退、重复映射与最终释放，共 9 项，全部通过。响应检查同时核对状态、Content-Length、正文及 fd 数量。

### 2 分段请求和连续请求解析不完整：已修复

位置：[httprequest.cpp](../code/http/httprequest.cpp) 与 [httpconn.cpp](../code/http/httpconn.cpp)。

- `parse()` 返回 `NeedMore`、`Complete`、`BadRequest`。
- 完整 CRLF 到达后才消费请求行或头字段；只有空行结束头部。
- 按完整、合法的 `Content-Length` 等待请求体；重复 Content-Length 一律拒绝，当前未支持的 Transfer-Encoding 明确拒绝。
- 头字段名统一小写，校验字段值中的控制字符；字段存储在字符检查循环结束后执行一次。
- 当前请求完成后才重置解析状态，保留属于下一请求的缓冲数据。
- Connection 选项按逗号拆分、去空白并忽略大小写；`close` 优先于 `keep-alive`。

分段位置遍历、非法请求、表单类型头和连续请求检查通过。此结论针对已覆盖的解析范围，不代表实现了全部 HTTP 特性。

### 3 Keep-Alive 重复响应泄漏文件映射：已修复

位置：[httpresponse.cpp](../code/http/httpresponse.cpp)，`HttpResponse::Init()`。

初始化下一响应前调用 `UnmapFile()`，再覆盖文件信息；文件映射由当前响应持有，并非共享文件缓存。重复生成 10 次响应仅保留当前映射，释放后无对应残留映射。

此结论不包含问题 6 中“发送过程中被其他线程关闭”的并发场景。

### 4 普通 HTTP 部分发送后提前关闭：已修复

位置：[webserver.cpp](../code/server/webserver.cpp)，`WebServer::OnWrite_()`。

在完成 TLS 专属状态和挂起操作处理后，HTTP 与 HTTPS 共用剩余数据检查。已有写入进展或遇到可重试错误、且仍有数据时，重新监听 `EPOLLOUT`，不因累计返回值为正而提前关闭。TLS 挂起写仍优先按 WANT_READ/WANT_WRITE 指定的方向恢复。

ET/LT、小发送缓冲区下的 8 KiB 与 4 MiB 响应检查通过。

### 5 TLS 关闭通知导致已读请求丢失：已修复

位置：[httpsconn.cpp](../code/http/httpsconn.cpp)、[httpconn.cpp](../code/http/httpconn.cpp) 与 [webserver.cpp](../code/server/webserver.cpp)。

- 使用独立的 `readClosed_` 记录读方向关闭，不依赖下次 I/O 会重置的临时状态。
- 正常读关闭后继续处理已读数据；完整请求生成响应，不完整请求终止。
- 允许处理缓冲区中剩余的连续请求，最终响应后结束连接。
- 优先恢复 TLS 挂起读写；无挂起操作时，已有响应且可写则优先发送，避免 `EPOLLOUT | EPOLLRDHUP` 组合事件丢掉响应。
- 致命 TLS 错误仍直接终止，不按正常读关闭继续处理。

相关 TLS 回归通过，包括对端关闭通知同时伴随 TCP FIN。这里解决的是收到对端通知后的请求处理；本端 `SSL_shutdown()` 的非阻塞重试仍是下面的独立待办。

### 6 关闭连接与工作线程生命周期协调：暂缓

位置：[webserver.cpp](../code/server/webserver.cpp)、[threadpool.h](../code/pool/threadpool.h) 及连接资源释放代码。

主线程可能因超时或错误关闭连接，而工作线程仍在读写、解析或访问文件映射；HTTPS 还可能在使用 `SSL*` 时被另一条路径释放。任务中的裸指针、旧定时器与 fd 复用也需要统一处理。

后续方向：

- 明确连接和资源所有者，区分关闭请求与最终释放，记录在途任务。
- 工作线程完成后通知管理 epoll 的线程，由其协调事件注册和资源释放。
- 任务、定时器及事件关联唯一连接 ID 或代次，避免旧操作影响复用 fd 后的新连接。
- 线程池提供停止与等待退出能力，服务器等待任务结束后再销毁连接、资源目录、SQL 池等依赖。

本项未进行并发压力复现或修复验收。后续应覆盖超时期间读写、对端中断、fd 复用和带在途任务退出，结合 ASan、TSan 检查。

## 其他状态与后续完善项

### 大文件长度：已补齐类型与分块处理

`ToWriteBytes()` 返回 `size_t`；I/O 返回值及上层接收变量使用 `ssize_t`。`HttpsConn::write()` 和 `IOBuffer::WriteSSL()` 均将单次提交给 `SSL_write()` 的长度限制为最多 16 KiB，再转换为 `int`。相关长度日志使用 `%zu`。

模拟 2 GiB 长度和调用参数检查通过；实际超大文件传输、慢速接收和性能测试仍未完成。

### 非阻塞 TLS 正常关闭：待实现

当前 `HttpsConn::Close()` 调用一次 `SSL_shutdown()` 后立即释放 `SSL*` 与 fd，没有恢复 WANT_READ/WANT_WRITE 的流程。

应区分正常关闭与立即释放，增加可恢复的关闭操作及关闭超时。返回 0 表示本端通知已发送，采用单向关闭策略时可以结束；返回 1 表示双向关闭完成；返回负值才根据 `SSL_get_error()` 判断等待方向或失败。发生致命 TLS 错误时不再尝试正常关闭握手。参见 [OpenSSL SSL_shutdown 文档](https://docs.openssl.org/3.0/man3/SSL_shutdown/)。

### 仓库回归测试：待整理

已同步 [模块说明](../code/http/readme.md) 和 [测试说明](../code/http/test/readme.md)，并新增文件响应回归 `httpresponseTest.cpp`：自动创建和清理临时资源，失败返回非零退出码。旧 HTTPS 测试源码尚未重构，其余临时回归仍需入库，后续还需真实 TLS 握手及覆盖上述边界的可重复测试入口。

## 提交整理

[.gitignore](../.gitignore) 已忽略本地证书目录、私钥、日志及构建产物；忽略规则不会取消跟踪已有文件。开发证书应在本机准备，不随源码提交。

复核时以下 8 个生成物仍被 Git 跟踪：

- `code/http/test/httpsconnTest`
- `code/pool/test/testThreadPool`
- `webbench-1.5/webbench`
- `webbench-1.5/webbench.o`
- `rss/css/.DS_Store`
- `rss/fonts/.DS_Store`
- `rss/images/.DS_Store`
- `rss/js/.DS_Store`

需要保留本地文件、仅取消后续跟踪时，可自行执行：

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

本次文档同步不执行以上命令。复核时暂存区为空；完整工作区编译通过不能替代最终提交内容的构建检查。

`git diff HEAD --check` 仍报告 `httpconn.cpp`、`httprequest.cpp` 中共 3 处尾随空白，待整理。

提交前：

- [ ] 整理尾随空白及已跟踪的生成物。
- [ ] 选择并暂存本次源码和文档，检查 `git diff --cached`。
- [ ] 确认证书私钥、运行日志和构建产物未被加入本次提交。
- [ ] 对最终准备提交的文件集合重新构建。

## 当前完成程度

可以描述为“HTTPS 基础能力已接入，主要请求解析、文件响应和读写边界已修复，仍有已知待办”。后续优先完成非阻塞 TLS 正常关闭及其余仓库回归测试；问题 6 继续单独安排。

如果 `master` 用作稳定版本，仍需结合这些待办和验证范围评估；个人学习进度可以保存阶段性提交，但合入本身不表示并发可靠性已验收。
