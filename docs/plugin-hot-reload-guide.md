# .so 运行时热更新

日期:2026-11-01.

当前已实现请求级模块快照、旧版本延迟释放、独立重载线程和 SIGHUP/signalfd 触发.默认只重载 plugin.path,服务器仍要求启动时 plugin.enabled = true.首次部署本轮主程序需要重新编译并重启一次;之后更新业务插件无需重启服务器.

## 1. 首次运行新版主程序

沿用已有数据库环境变量和 TLS 配置,先停止旧版服务器.在项目根目录执行 make 编译本轮主程序.本机 config/server.toml 中先配置一个已编译的插件:

```toml
[plugin]
enabled = true
path = "../plugins/business.v1.so"
```

如果 v1 已存在,继续使用该文件.新部署且尚无此文件时,可以 `make -C plugins VERSION=v1` 首次构建.

在同一个终端启动服务器并保存实际 PID:

```bash
bin/server --config config/server.toml &
serverPid=$!
```

确认正常启动后,沿用原有 HTTP/HTTPS 地址访问 /api/version,应返回当前插件的正文.启用插件的启动过程会注册 SIGHUP 重载入口;启动时禁用插件的进程需要重启才能开启该能力.

## 2. 构建并发布新版本

例如构建一个此前尚未使用的 v2:

```bash
make -C plugins VERSION=v2
```

生成 plugins/business.v2.so.示例插件通过编译宏返回 TinyWebServer v2,业务版本标签与 plugin_api.h 的 ABI 版本独立.

构建先写入 plugins/ 下的独立临时目录,链接成功后用硬链接原子发布新名字.已有目标文件不会被替换,构建失败不会发布半成品.源代码变化后不要复用旧标签,改用 v3 等新标签.这是本机同一文件系统内的发布方式,无需安装到系统库目录.

## 3. 修改目标路径并发出信号

把本机 config/server.toml 的插件路径改为:

```toml
[plugin]
enabled = true
path = "../plugins/business.v2.so"
```

完成文件保存后,在保存了 serverPid 的同一终端发送:

```bash
kill -HUP "$serverPid"
```

如果使用其他终端,需要先把 serverPid 设为目标服务器的实际 PID.不要向尚未升级到本轮代码的旧进程发送此信号.

信号只提交重载请求,返回不表示重载已完成.重新请求 /api/version,确认返回 v2.启用日志时,log/ 中会记录 Business plugin reloaded 或 Plugin reload failed.继续更新时重复发布新文件、修改路径和发送信号即可.

## 4. 运行机制

```text
main在创建线程前屏蔽SIGHUP
    → 日志线程、工作线程和重载线程继承信号屏蔽
    → signalfd接收SIGHUP,epoll主循环识别并排空
    → PluginReloader提交/合并任务,由独立线程读取配置
    → PluginManager加载、校验并初始化候选Module
    → 原子发布当前模块引用
```

对应实现:

- [PluginManager](../code/plugin/pluginmanager.cpp):用 atomic<shared_ptr<Module>> 发布和取得当前版本;重载操作用互斥锁串行执行.
- [PluginReloader](../code/plugin/pluginreloader.cpp):合并等待期间的重复请求,隔离加载耗时,捕获失败,析构时等待活动任务结束.
- [ReloadSignal](../code/server/reloadsignal.cpp):创建非阻塞、CLOEXEC 的 signalfd,处理 EINTR 并排空到 EAGAIN.
- [HttpConn](../code/http/httpconn.cpp):保存管理器,每次解析完成后取得一次快照,交给本次 RequestHandler.
- [WebServer](../code/server/webserver.cpp):先识别重载 fd,再处理监听与客户端事件;退出时先停止重载线程.

已有 keep-alive 连接也会在下一次业务分派时取得新版.同一次请求的全部插件调用使用同一份快照;旧请求执行期间其模块引用保持有效.最后一个引用释放后,Module 先销毁实例再 dlclose.正文已复制到主程序存储,网络发送不会引用已释放的插件字符串.

## 5. 失败和范围

重载读取启动时同一个配置文件的绝对路径,使用现有 LoadConfig 完整解析和校验,只应用 plugin.path.端口、日志、数据库、TLS、资源目录和线程数继续使用启动值,更改这些字段需要重启.运行中设为 plugin.enabled = false 会拒绝本次重载,继续使用原插件.

文件不存在、配置无效、ABI 不匹配或 create 失败时,当前模块保持不变.修复配置后可以再次发送 SIGHUP,重载线程仍可使用.插件正常加载后的单次业务执行失败仍返回 HTTP 500,不会自动用旧版重试.

标准信号及等待中的任务都可能合并,一次信号不对应一个必须执行的版本.如需逐版本验收,等待当前目标生效后再切换下一个版本.

插件当前不创建后台线程、不保存异步回调,长期数据放在主程序或数据库中.共享引用保护插件生命周期,不会修复已有的连接与工作线程关闭协调问题.加载器释放库引用也不保证任意库都立即解除映射.参考 [signalfd](https://man7.org/linux/man-pages/man2/signalfd.2.html)、[线程信号屏蔽](https://man7.org/linux/man-pages/man3/pthread_sigmask.3.html) 和 [dlopen/dlclose](https://man7.org/linux/man-pages/man3/dlopen.3.html).

## 6. 验证

根目录的七个测试入口合计 271 项检查,其中:

- plugin-test:26 项 Module 回归、14 项管理器及重载线程回归,以及独立加载程序.
- http-test:17 项内存连接处理链检查,包含同一 keep-alive 连接切换、分段请求的版本选择、失败保留旧版及 HTTPS 复用分派.
- server-test:23 个启动/运行场景和 5 项真实信号检查.使用模拟数据库和监听,真实 signalfd、epoll、配置重读与重载线程,验证信号发布新版和失败修复后重试.

管理器测试在插件函数执行到一半时阻塞旧请求,发布新版后检查旧实例仍有效,解除阻塞后检查其销毁时机.另有并发请求/重载、初始化阻塞时旧版本可用、任务合并和退出等待检查.

上述回归不绑定 TCP 端口,不连接真实数据库.实际手机/浏览器访问、真实数据库业务和完整服务器并发关闭仍需单独验收.
