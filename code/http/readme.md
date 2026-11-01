# HTTP / HTTPS 模块说明

更新日期：2026-10-27。以下按当前源码说明；修复状态及未完成项见 [HTTPS 已知问题](../../docs/https-known-issues.md)，测试方式见 [测试说明](test/readme.md)。

## 请求解析：HttpRequest

HTTP 请求行和每个头字段行以 `\r\n` 结束；头部末尾还有一条空行。因此，最后一个头字段后通常看到 `\r\n\r\n`。请求体由消息长度决定，不按行读取。

```text
POST /submit HTTP/1.1\r\n
Host: localhost\r\n
Content-Type: application/x-www-form-urlencoded\r\n
Content-Length: 3\r\n
\r\n
a=1
```

上例中的 `\r\n` 表示实际回车换行字节；请求体为 3 字节。

### 状态与返回结果

内部解析状态依次为 `REQUEST_LINE → HEADERS → BODY → FINISH`。`parse(Buffer&)` 对外返回 `ParseRes`：

| 结果 | 含义 | 调用方处理 |
| --- | --- | --- |
| `NeedMore` | 当前数据不足，尚不能确定完整请求 | 保留解析状态与未消费字节，继续接收 |
| `Complete` | 当前请求已完整解析 | 准备响应 |
| `BadRequest` | 请求格式或当前支持的消息定界不合法 | 准备 400 响应并结束连接 |

源码仍保留旧的 `HTTP_CODE` 枚举，但当前 `parse()` 的返回类型是 `ParseRes`。

- `ParseRequestLine_()` 提取方法、原始路径和版本,首页和页面别名映射移到业务层,解析器不再修改业务路径.
- `ParseHeader_()` 校验字段名和字段值，头字段名统一转为小写。先检查全部字符，再保存一次字段。
- `FinishHeadrs_()` 在空行处校验消息定界：拒绝当前未支持的 Transfer-Encoding，用 `from_chars` 完整解析非负 Content-Length，拒绝重复或非法长度；缺失长度时请求体长度为 0。
- BODY 状态等待完整的 `contentLength_` 字节，消费当前请求体，不吞掉下一请求的数据。
- `IsFormUrlencoded()` 识别表单类型及媒体类型参数.POST 表单由 `ParseFromUrlencoded_()` 解码到 post_,原始 body_ 保持不变.解析器不依赖 MySQL 头文件或客户端库,不执行数据库访问或登录注册.
- 表单按 & 分隔,按第一个 = 区分键值,+ 解码为空格,%HH 解码为字节,重复键保留最后一个值.不完整或非法的百分号编码返回 BadRequest,不记录含密码的请求正文或表单值.
- `Init()` 清空请求数据并回到 REQUEST_LINE。分段接收时不能每次调用它，否则会丢失前一段的解析状态。

`IsKeepAlive()` 当前要求 HTTP/1.1 且 Connection 中有完整的 `keep-alive` 选项，同时没有 `close`。选项按逗号拆分、去除两端空格/制表符并忽略大小写；当前实现并不默认保持所有 HTTP/1.1 连接。

## 文件响应：HttpResponse

`Init(srcDir, path, isKeepAlive, code)` 先释放旧映射，再初始化当前响应。`MakeResponse(Buffer&)` 准备状态行、响应头及文件内容引用。

文件路径通过 `OpenFile_()` 打开：

1. 资源目录以目录 fd 表示。
2. 去掉 URL 路径开头的 `/`，相对于目录 fd 调用 Linux `openat2`。
3. 使用 `RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS` 限制越出资源目录和符号链接。
4. 对已打开的 fd 使用 `fstat`，确认普通文件及当前代码要求的读取权限；错误页面也走同样的打开路径。

这一路径依赖 Linux 内核支持 `openat2`，目前没有旧内核兼容回退。

`MapFile_(int srcFd)` 接收并接管已经打开的 fd：

- 空文件直接关闭 fd 并返回成功，不建立映射。
- 非空文件用 `mmap(PROT_READ, MAP_PRIVATE)` 建立映射，失败时比较 `MAP_FAILED`，不解引用失败指针。
- 成功和失败路径均关闭 fd；成功的映射继续由当前响应对象持有，失败时清空指针和长度、设置状态 500 并返回 false。
- `File()` 返回映射地址，`FileLen()` 返回记录的文件长度；`UnmapFile()` 释放映射。

`MakeResponse()` 先确定文件映射结果，再生成状态行和响应头。文件准备成功时输出对应 Content-Length；无法准备文件时生成内联 HTML 错误正文，其 Content-Length 按实际正文计算。映射失败返回 `500 Internal Server Error`，内联错误响应使用 HTML 类型。

业务已经确定的错误状态不会因错误页面缺失而被覆盖,例如没有 400.html 时仍返回内联 400 正文.MakeTextResponse() 支持业务提供的自有正文和 Content-Type,释放旧映射并将正文复制到 writeBuff_;长度按字节计算,拒绝带控制字符的类型字段.

文件正文没有复制进 `writeBuff_`。每个响应持有自己的映射，这不是跨连接的文件缓存。只有旧响应不再被发送使用时，才能释放其映射。

普通文件、空文件、映射失败及 404 回退等行为由 [文件响应回归](test/httpresponseTest.cpp) 覆盖。

响应只声明实际的 `Connection: keep-alive` 或 `Connection: close`,不再发送没有对应行为支持的固定 `max=6, timeout=120` 参数.空闲超时继续由服务器定时器配置控制.

## 普通连接：HttpConn

`Init(fd, addr)` 初始化连接，清空读写缓冲区、解析状态及两段 `iovec`。

| 成员或接口 | 当前作用 |
| --- | --- |
| `readBuff_` / `writeBuff_` | `IOBuffer`，分别保存收到的数据和待发送的响应头/内联错误正文 |
| `iov_[0]` | 指向 writeBuff_ 中尚未发送的部分 |
| `iov_[1]` | 指向文件映射中尚未发送的部分 |
| `requestDone_` | 标识上一请求是否已完成解析，用于下一次处理时重置请求 |
| `keepAlive_` | 当前响应完成后是否继续处理连接 |
| `ToWriteBytes()` | 返回两个 iovec 剩余长度之和，类型为 size_t |
| `read()` / `write()` | 返回 ssize_t；有进展时返回本轮累计字节数，否则返回最后一次结果 |

`process()` 的处理流程：

1. 仅在上一请求已完成后重置请求解析器。
2. 调用 `parse()`；NeedMore 时返回 false，暂不构造响应。
3. Complete 时调用 RequestHandler,根据 HandlerResult 选择文件或文本响应;BadRequest 时准备 400 响应,不调用业务层.
4. 结合请求的 Keep-Alive 选项、读方向关闭状态和缓冲中的后续数据决定 `keepAlive_`。
5. 构造响应，并让 iovec 指向响应缓冲区和可用的文件映射。

`write()` 使用 `writev`，成功后推进对应 iovec 和缓冲区位置。返回正数仍可能有数据未发完，上层必须检查 `ToWriteBytes()`。`Close()` 释放当前映射并关闭 fd。

## TLS 连接：HttpsConn 与 SSLContext

`SSLContext` 加载服务器证书和私钥、检查匹配并创建共享的 `SSL_CTX`.候选上下文全部校验成功后才发布,失败返回 false,通过 `GetLastError()` 保留具体原因,不发布半成品.

`caPath` 只指定客户端证书的信任源,`requireClientCertificate` 独立控制强制验证,默认 false.开启时必须提供 CA,并设置 `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`;关闭时使用 `SSL_VERIFY_NONE`,即使配置了 CA 也不要求客户端证书.这些设置不等同于让浏览器信任服务器证书.仓库内的 [上下文测试](test/sslcontextTest.cpp) 通过真实内存 TLS 握手覆盖这些模式.

每条 `HttpsConn` 通过 `InitSSL()` 创建独立的 `SSL*`、绑定非阻塞 fd，再由服务器调用 `SSLHandShake()` 驱动握手。握手返回 Done、WantRead、WantWrite 或 Failed。

握手完成后，读写同时维护：

| 状态 | 含义 |
| --- | --- |
| `IOState::Ready` | 当前没有等待事件或失败 |
| `IOState::WantRead / WantWrite` | 恢复操作所需的事件方向 |
| `IOState::Closed / Failed` | 当前操作遇到正常关闭或致命失败 |
| `PendingOp::Read / Write` | 需要恢复的原始操作 |
| `readClosed_` | 持久记录对端已停止发送应用数据，不随下次读写重置 |

等待方向与操作类型不能混为一谈：SSL_write 遇到 WANT_READ 时，应等待可读后恢复写操作。

TLS 读取使用最多 16 KiB 的缓冲区；`HttpsConn::write()` 和 `IOBuffer::WriteSSL()` 均限制单次提交最多 16 KiB。仅成功发送后推进指针，WANT 重试时保留原来的数据和长度。

正常读关闭后，已收到的完整请求仍可生成响应；请求不完整且不会再有数据时结束连接。本端非阻塞正常关闭的重试，以及线程与资源生命周期协调尚未实现。

## 与 WebServer 的配合

- 握手尚未完成时，读写入口先驱动握手。
- 事件分发优先恢复 TLS 挂起操作；无挂起操作时，已有响应且可写则先发送，再处理普通可读/读关闭事件。
- `OnRead_()` 先处理 TLS 状态，再进入请求处理；读到正常关闭不直接丢弃缓冲数据。
- `OnProcess()` 为已构造响应监听 EPOLLOUT，为需要更多数据的请求监听 EPOLLIN。
- `OnWrite_()` 先处理 TLS 状态和挂起操作，再进行 HTTP/HTTPS 共用的剩余数据检查；发送完成后根据 Keep-Alive 决定继续处理或关闭。

登录、注册依赖正确配置的 MySQL 连接池；本轮 HTTP/TLS 边界回归未覆盖全部数据库业务。

业务路由和参数化数据库操作见 [handler 模块](../handler/readme.md).文本与文件响应切换、连续请求和资源目录约束由 httpconnProcessTest 覆盖,其中仅在内存中驱动处理链,没有创建网络连接.
