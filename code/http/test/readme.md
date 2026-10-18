# HTTP / HTTPS 测试说明

更新日期：2026-10-19。当前 `httpsconnTest.cpp` 是早期接口实验程序，尚不能作为 HTTPS 回归或合入验收入口。已知问题与验证结果见 [修复清单](../../../docs/https-known-issues.md)。

## 文件响应回归

从项目根目录编译并运行：

```bash
g++ -std=c++23 -pthread -g -O0 -Wall \
  code/http/test/httpresponseTest.cpp \
  code/http/httpresponse.cpp \
  code/buffer/buffer.cpp \
  code/log/log.cpp \
  -Wl,--wrap=mmap \
  -o /tmp/tinyweb-httpresponse-test
/tmp/tinyweb-httpresponse-test
```

`httpresponseTest.cpp` 自动创建和清理临时站点，不依赖数据库、证书或运行中的服务器。通过链接器 `--wrap=mmap` 模拟 ENOMEM，仅影响测试二进制。任一检查失败返回非零退出码。

当前 9 项回归覆盖普通文件、空文件、mmap 失败时的 500 与状态清理、图片请求失败后的 HTML 类型、失败后的下一次响应、映射的 404 页面、内联 404 回退、重复映射与最终释放；同时检查响应长度、正文和 fd 数量。

## 编译旧测试程序

从项目根目录执行，使用与项目构建一致的 C++23，并链接 MySQL、OpenSSL 和 pthread：

```bash
g++ -std=c++23 -pthread -g -O0 -Wall -Wextra \
  code/http/test/httpsconnTest.cpp \
  code/http/*.cpp \
  code/buffer/*.cpp \
  code/log/log.cpp \
  code/pool/sqlconnpool.cpp \
  -o /tmp/tinyweb-httpsconn-test \
  -lmysqlclient -lssl -lcrypto
```

产物放在 `/tmp`，避免覆盖仓库中仍被跟踪的旧二进制。旧的 C++17 命令不适用于当前源码中的 `unordered_map::contains()` 等调用。

该命令已验证可编译、链接；启用 `-Wextra` 后仍有现有结构体初始化写法的警告。编译成功不代表下面的测试逻辑已更新，也不代表运行结果可作验收依据。

## 旧测试的现有限制

- 证书上下文初始化被注释，连接测试没有建立真实 TLS 握手。
- 部分错误断言仍按普通 socket 的 errno（例如 EBADF）判断，没有按当前 TLS 错误语义处理。
- 空发送结果的假设与当前实现不完全一致。
- 部分用例使用 123、456、999 等数字模拟 fd；另一些用例手动关闭了连接对象拥有的 fd，资源所有权需要重构。
- `TestHttpOverHttpsSimulation()` 发送的是明文 HTTP，没有建立 TLS 会话，也没有纳入当前 `runAllTests()`。
- 若干失败仅打印消息，`main()` 最后仍返回 0；“All Tests Completed”不能证明测试成功。

在测试程序重构前，应结合修复清单中的验证记录判断当前进度。

## 已做的临时回归

以下程序尚未整理进仓库，没有可在本目录直接执行的统一入口：

| 范围 | 已验证内容 |
| --- | --- |
| 请求解析 | GET/POST 分段、头部与请求体边界、非法或重复 Content-Length、Transfer-Encoding 拒绝、连续请求、Connection 选项 |
| HTTP 写入 | 小发送缓冲区下，ET/LT 各发送 8 KiB 与 4 MiB 响应，检查接收完整性 |
| TLS | 真实 TLS 1.3 会话的 14 个场景，包括关闭通知、TCP FIN、挂起写、连续请求、异常 EOF、空错误指针和握手失败状态 |
| 长度与分块 | 模拟 2 GiB 剩余长度，检查两处 SSL_write 的单次长度不超过 16 KiB |

文件响应回归已在上节纳入仓库。其余临时 HTTP/TLS 测试采用单线程回调驱动，未验证线程池、定时器与关闭资源的并发行为，也未实际传输 2 GiB 文件。

## 手动 HTTPS 页面检查

先按项目配置启动服务器，并确保本机已有开发证书。下面以端口 8888 为例，从项目根目录执行。

如果服务器证书由本地开发 CA 签发，用根 CA 证书建立客户端信任：

```bash
curl --cacert ./ssl/dev-root-ca.crt https://localhost:8888/
```

如果直接使用自签名服务器证书，则使用对应的证书：

```bash
curl --cacert ./ssl/server.crt https://localhost:8888/
```

证书中的主机名应匹配访问地址。上述命令用于普通单向 HTTPS；若服务器配置了客户端证书校验，还需要相应的客户端证书和私钥。

获取 HTML 说明本次证书校验、握手和页面响应成功，不覆盖慢速发送、连续请求、并发超时及关闭重试等边界。

## 后续回归入口要求

- 使用真实且明确归属的 fd，正确初始化 TLS 上下文并完成握手。
- 自动准备临时站点资源和测试证书，不依赖开发者的私钥。
- 失败用例返回非零退出码，检查响应内容、长度与连接状态。
- 将上述临时用例整理为可重复运行的脚本或测试目标。
- 保留映射失败回归，并为非阻塞 SSL_shutdown 和并发关闭分别补充验收。
