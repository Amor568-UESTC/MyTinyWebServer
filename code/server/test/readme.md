# 启动失败测试

本目录执行 `make test`,或在项目根目录执行 `make server-test`.默认生成 serverStartupTest、reloadsignalTest 与重命名 main 后的 serverMain.o,make clean 清理这些产物.可以用 BIN_DIR 指定输出目录.

测试调用真实 main、Loader 和 WebServer 构造流程,仅在测试二进制中通过链接器 wrap 替换 MySQL 与网络操作.不连接真实数据库,不创建网络监听,使用真实文件 fd 检查回收和重复关闭.

23 个场景覆盖 MySQL 库、日志、数据库、TLS、socket、两种 setsockopt、非阻塞设置、bind、listen、epoll 创建和注册失败,以及插件路径、库文件、ABI、create 失败和后续回收.新增信号屏蔽、signalfd 创建和注册失败,以及真实 SIGHUP 触发运行时更新和无效配置修复后重试.测试检查退出码、终端原因、密码不泄漏、资源回收和 fd 数量.

两个运行场景使用模拟监听和真实 signalfd/epoll,重写临时配置并向自身发送 SIGHUP,实际调用 WebServer::Start 和 PluginReloader.观察新版发布后由测试主动结束循环,验证线程退出、旧/新实例和 fd 清理.不绑定 TCP 端口.

reloadsignalTest 另有 5 项检查:信号屏蔽继承、NONBLOCK/CLOEXEC、空队列、真实 SIGHUP 通过 epoll 接收并排空、析构关闭 fd.

插件桩库在本目录 plugin-fixtures/ 中生成,通过实际 dlopen 加载,记录创建、销毁和库卸载顺序.验证插件初始化失败前不创建监听,也验证后续启动失败释放已创建的插件实例和库句柄.

日志使用临时工作目录,测试不读取本机 server.toml 或证书.依赖与主程序一致: C++23、pthread、MySQL 客户端库、OpenSSL、pkg-config 和系统 toml++.

这些是失败路径回归,不替代真实账号下的正常启动、HTTP/HTTPS 页面、登录注册与并发关闭验收.
