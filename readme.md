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

在本机配置中填写 `[database]` 的主机、端口、用户、密码、数据库名和连接池大小。默认启用数据库连接池，普通静态页面访问也会先初始化连接池。

配置字段使用驼峰命名并区分大小写，未知字段和类型错误会阻止启动。`documentRoot`、证书和私钥等配置内的相对路径，以配置文件所在目录为基准；模板中的 `../rss` 指向项目资源目录。

编译并启动：

```bash
make
bin/server --config config/server.toml
```

不带参数时同样读取 `config/server.toml`；`bin/server --help` 查看启动格式。跨目录启动时，使用可执行文件和配置文件的绝对路径。日志当前仍写入启动工作目录下的 `./log`。

开启 HTTPS 时，在本机准备证书和私钥，将 `[tls]` 的 `enabled` 设为 `true`，并填写 `certificate` 和 `privateKey`。普通单向 HTTPS 将 `clientCaFile` 留空；当前该字段非空会要求客户端提供受信任的证书。证书与私钥不随仓库分发。

当前支持启动时 TOML 配置，修改后需要重新启动服务器。环境变量密码、`.env`、运行中配置重载和 `.so` 热更新尚未实现。实施进度见 [配置化与热更新指南](docs/config-and-hot-reload-guide.md)，已知限制见 [HTTPS 问题清单](docs/https-known-issues.md)。

配置读取回归不依赖数据库服务和真实证书：

```bash
make config-test
```

该目标运行 42 项配置检查。HTTP/HTTPS 的其他验证方式见 [测试说明](code/http/test/readme.md)。

本机 `config/server.toml`、`.env`、证书、私钥和构建产物已由 `.gitignore` 忽略。`.vscode` 只共享 C++ 提示配置和文件类型关联，其他本地配置忽略。

先停止旧服务器再重新构建或清理。以下命令会删除 bin 和 log 目录：

```bash
make clean
```

## 压力测试
```bash
./webbench-1.5/webbench -c 10000 -t http://ip:port/
```
