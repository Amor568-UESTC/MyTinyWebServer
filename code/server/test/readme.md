# 启动失败测试

本目录执行 `make test`,或在项目根目录执行 `make server-test`.默认生成 `serverStartupTest` 与重命名 main 后的 `serverMain.o`,`make clean` 清理二者.可以用 `BIN_DIR` 指定输出目录.

测试调用真实 main、Loader 和 WebServer 构造流程,仅在测试二进制中通过链接器 wrap 替换 MySQL 与网络操作.不连接真实数据库,不创建网络监听,使用真实文件 fd 检查回收和重复关闭.

18 个场景覆盖 MySQL 库、日志、数据库、TLS、socket、两种 setsockopt、非阻塞设置、bind、listen、epoll 创建和注册失败.新增插件路径缺失、无效库文件、错误 ABI、create 失败,以及插件成功加载后监听失败的回收.其中分别验证日志开启和关闭时的数据库错误.每项都检查退出码 1、终端原因、错误中不含测试密码、数据库连接回收和 fd 数量.

插件桩库在本目录 plugin-fixtures/ 中生成,通过实际 dlopen 加载,记录创建、销毁和库卸载顺序.验证插件初始化失败前不创建监听,也验证后续启动失败释放已创建的插件实例和库句柄.

日志使用临时工作目录,测试不读取本机 server.toml 或证书.依赖与主程序一致: C++23、pthread、MySQL 客户端库、OpenSSL、pkg-config 和系统 toml++.

这些是失败路径回归,不替代真实账号下的正常启动、HTTP/HTTPS 页面、登录注册与并发关闭验收.
