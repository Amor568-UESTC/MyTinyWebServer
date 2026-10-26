# MyTinyWebServer
主要学习了后端的部分，前端部分参考代码实现
使用C++实现的轻量级服务器，可以实现成千上万的连接数目

## 环境

* Linux
* 支持 C++23 的 g++，以及 make、pkg-config
* MySQL 客户端开发库，以及可连接的 MySQL 服务
* OpenSSL 开发库
* 系统安装的 toml++，当前使用 v3.4.0，提供 `<toml++/toml.hpp>` 和 `tomlplusplus.pc`

构建使用 `pkg-config` 获取 toml++ 的编译、链接参数。可以先检查：

```bash
pkg-config --modversion tomlplusplus
pkg-config --cflags --libs tomlplusplus
```

## 主要目录

```text
MyTinyWebServer/
├── Makefile
├── build/Makefile
├── code/
│   ├── main.cpp
│   ├── config/       配置结构、TOML Loader 与配置测试
│   ├── buffer/       普通与 TLS I/O 缓冲
│   ├── http/         HTTP 解析、响应和 HTTP/HTTPS 连接
│   ├── log/          日志与异步队列
│   ├── pool/         线程池与数据库连接池
│   ├── server/       epoll 与服务器事件分派
│   └── timer/        连接定时器
├── config/
│   └── server.example.toml
├── docs/             配置化路线与 HTTPS 已知问题
├── rss/              静态网页、样式、图片与视频
└── webbench-1.5/      压力测试工具
```

本机另行准备 `config/server.toml`、HTTPS 所需的 `ssl/` 证书目录，以及运行时生成的 `bin/`、`log/`。

## 项目启动

先连接到本机 MySQL，创建数据库和业务表：

```sql
CREATE DATABASE IF NOT EXISTS yourdb;
USE yourdb;

CREATE TABLE IF NOT EXISTS user (
    username char(50) null,
    password char(50) null
);
```

首次使用时，在项目根目录复制配置模板；已有 `config/server.toml` 时直接编辑原文件：

```bash
cp config/server.example.toml config/server.toml
```

在本机配置中填写 `[database]` 的主机、端口、用户、数据库名和连接池大小。模板使用 `passwordEnv = "TINYWEB_DB_PASSWORD"` 指定密码的环境变量名称，真实密码由启动环境提供。默认启用数据库连接池，普通静态页面访问也会先初始化连接池。

原有 `password = "..."` 写法仍兼容；迁移时删除该字段，改为 `passwordEnv`，两项不能同时存在。指定的环境变量未设置或为空时，Loader 会明确报错。原有空密码配置继续允许使用 `password = ""`。

配置字段使用驼峰命名并区分大小写，未知字段和类型错误会阻止启动。`documentRoot`、证书和私钥等配置内的相对路径，以配置文件所在目录为基准；模板中的 `../rss` 指向项目资源目录。

在 Bash 终端逐条执行以下命令；`read` 会等待输入密码，输入内容不回显。`export` 使启动的服务器进程能够读取该变量：

```bash
make
read -r -s -p 'MySQL password: ' TINYWEB_DB_PASSWORD
printf '\n'
export TINYWEB_DB_PASSWORD
bin/server --config config/server.toml
```

不带参数时同样读取 `config/server.toml`；`bin/server --help` 查看启动格式。跨目录启动时，使用可执行文件和配置文件的绝对路径。日志当前仍写入启动工作目录下的 `./log`。

开启 HTTPS 时，在本机准备证书和私钥，将 `[tls]` 的 `enabled` 设为 `true`，并填写 `certificate` 和 `privateKey`。普通单向 HTTPS 使用 `requireClientCertificate = false`,默认 `clientCaFile` 留空.需要双向 TLS 时设为 true,并填写用于验证客户端的 CA;原先依赖非空 CA 路径开启验证的配置需要显式增加此开关.证书与私钥不随仓库分发。

日志级别 0/1/2/3 分别为 DEBUG/INFO/WARN/ERROR,按最低输出级别过滤.`asyncQueueSize = 0` 使用同步写入,正数指定异步队列容量.日志在数据库实际连接之前初始化;日志、数据库、TLS、epoll 或监听初始化失败时,终端会输出原因,进程返回 1.

当前支持启动时 TOML 配置和进程环境变量密码，修改后需要重新启动服务器。环境变量值会复制到 `ServerConfig`，改变密码来源并不等于加密，也不会改变 MySQL 的账号认证方式。`.env` 文件读取、运行中配置重载和 `.so` 热更新尚未实现。实施进度见 [配置化与热更新指南](docs/config-and-hot-reload-guide.md)，已知限制见 [HTTPS 问题清单](docs/https-known-issues.md)。

仓库的命名、注释、排版及测试目录约定见 [风格Skill参考](docs/skills/tinyweb-style/SKILL.md).

配置读取回归不依赖数据库服务和真实证书：

```bash
make config-test
```

该目标运行 59 项配置检查。HTTP/HTTPS 的其他验证方式见 [测试说明](code/http/test/readme.md)。

数据库连接池和无连接登录失败路径的回归：

```bash
make sql-test
```

该目标运行 15 个模拟 MySQL API 的场景，不连接真实数据库。连接池采用池空时返回失败的策略，互斥锁只保护空闲连接队列，执行 SQL 时不持有锁。测试范围和关闭时的生命周期限制见 [连接池说明](code/pool/readme.md)。

测试统一放在对应模块的 `test/` 小目录中,各目录自带 Makefile 和说明,默认测试程序也在本目录生成:

| 测试目录 | 默认程序 | 根目录转发命令 |
| --- | --- | --- |
| `code/config/test/` | `configloaderTest` | `make config-test` |
| `code/pool/test/` | `sqlconnpoolTest` | `make sql-test` |
| `code/http/test/` | `httpresponseTest` | `make http-test` |
| `code/http/test/` | `sslcontextTest` | `make http-test` |
| `code/log/test/` | `logTest` | `make log-test` |
| `code/server/test/` | `serverStartupTest` | `make server-test` |

可以进入任意上述目录,用 `make` 编译、`make test` 编译并运行、`make clean` 清理该 Makefile 生成的程序.原有 ThreadPoolV2 和 HTTPS 实验测试继续保留原来的单独编译方式,具体见各 test 目录的说明.

五个根目录入口共 116 项检查:配置 59、连接池 15、文件响应 11、TLS 上下文及握手 10、日志 8、启动失败 13.TLS 测试自动签发临时证书并通过内存 BIO 握手,启动测试使用模拟依赖,均不需要本机数据库密码或占用服务端口.真实数据库业务与并发关闭问题仍需独立验收.

本机 `config/server.toml`、`.env`、证书、私钥和构建产物已由 `.gitignore` 忽略。`.vscode` 只共享 C++ 提示配置和文件类型关联，其他本地配置忽略。

可以在旧服务器运行时执行 `make`。构建只将 `.cpp` 作为编译输入，先在输出目录中的独立临时目录生成可执行文件，成功后通过重命名替换 `bin/server`；编译或链接失败时保留原文件。

旧进程继续运行原来的代码，新版本需要停止旧进程并重新启动后才会生效。重编译不会自动更新正在运行的进程。

建议停止服务器后再清理。以下命令会删除 bin 和 log 目录,并清理上述各 test 目录的 Makefile 生成的测试程序:

```bash
make clean
```

## 压力测试
```bash
./webbench-1.5/webbench -c 10000 -t http://ip:port/
```
