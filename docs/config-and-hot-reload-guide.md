# 配置化与 .so 热更新实践指南

首次记录：2026-10-20。最近同步：2026-10-24。本文同时记录当前实现和后续实施路线，尚未实现的部分在各阶段单独标明。

| 阶段 | 当前状态 |
| --- | --- |
| 一：配置结构 | 已接入，`WebServer` 持有 `const ServerConfig`，替代位置参数和重复配置成员 |
| 二：TOML Loader | 已实现读取、严格校验、路径解析和 `--config`；仓库内 42 项 Loader 回归通过 |
| 三：环境变量与可选 `.env` | 待实现；当前密码读取自 TOML 的 `database.password` |
| 四：模块行为与初始化 | 部分接入，数据库失败处理、日志初始化和实际参数使用仍需完善 |
| 五～八：业务拆分、插件加载与热更新 | 待实现，下面的接口和命令属于实施计划 |

当前适合提交配置结构与 TOML Loader 的阶段性成果。测试验证了配置解析与校验；真实数据库、HTTP/HTTPS 启动和模块行为仍按阶段四验收。

第一轮目标是从 `main()` 移出部署参数；第二轮目标是支持路由和业务代码的 `.so` 热更新。连接、epoll、HTTP 解析、SSL 读写和资源管理继续由主程序持有。

现有并发与关闭问题继续按 [HTTPS 已知问题](https-known-issues.md) 中的问题 6 单独安排。插件引用计数只保护插件自己的生命周期，不会顺带修复连接裸指针、fd 复用或 SSL 资源竞争。

## 1. 先明确三种不同能力

| 能力 | 改动内容 | 本文安排 |
| --- | --- | --- |
| 启动配置 | 启动时读取端口、路径、数据库等参数 | 第一轮实现；修改后重新启动 |
| 配置重载 | 运行中发布新的配置值 | 插件基础完成后，选择支持的字段实现 |
| 代码热更新 | 运行中加载新 `.so`，让后续业务调用使用新版 | 第二轮实现；在途调用继续使用旧版 |

资源目录中的 HTML、CSS、图片是普通文件，更新这类内容通常不需要 `.so`。证书更新也不需要业务插件，应通过替换 TLS 上下文实现，见第 11 节。

## 2. 当前适合配置化的项目

| 分组 | 当前支持字段 | 当前行为或限制 |
| --- | --- | --- |
| 监听 | `server.port` | 默认 8888；IPv4 地址固定为 `INADDR_ANY`，backlog 固定为 6，连接数上限为 65536 |
| 连接 | `server.idleTimeoutMs`、`listenMode`、`connectionMode`、`linger` | 默认超时 60000 ms、监听与连接均为 ET、关闭 linger；超时 0 表示关闭空闲定时器 |
| 资源 | `server.documentRoot` | 路径由 Loader 解析，`WebServer` 使用配置持有的字符串 |
| 线程 | `server.workerThreads` | 默认 6，传入线程池 |
| 数据库 | `database.host`、`port`、`user`、`password`、`name`、`poolSize` | 已传入连接池；用户和库名必须非空，密码允许为空，连接失败处理待完善 |
| TLS | `tls.enabled`、`certificate`、`privateKey`、`clientCaFile` | 开启时检查证书文件类型；非空 `clientCaFile` 沿用强制验证客户端证书的行为 |
| 日志 | `logging.enabled`、`level`、`asyncQueueSize` | 已传入日志初始化；队列容量尚未真正传给队列，目录和轮转行数仍写死 |

字段采用驼峰命名，并且区分大小写；同一行未重复的分组前缀与首字段一致。未来可增加 `server.address`、`listenBacklog`、`maxConnections`、`database.passwordEnv`、`tls.requireClientCertificate`、`logging.directory`、`rotateLines` 和 `plugin.path`。这些字段当前都会被未知字段检查拒绝，不能直接写入本机配置。

以下参数以后可以补，但必须同时实现对应行为：请求头与请求体大小上限、每条连接的请求数上限、握手超时、日志控制台输出。不能只添加配置项就宣称能力已经存在。

HTTP 语法、CRLF、解析状态、SSL 错误处理及安全检查继续保留在代码里。

## 3. 阶段一：先建立配置结构，暂不解析文件

状态：配置结构与构造接口已实现。以下保留实施过程，当前 `main()` 已改为通过 Loader 取得配置。

### 修改位置

- 新建 `code/config/serverconfig.h`。
- 修改 [main.cpp](../code/main.cpp) 和 [webserver.h](../code/server/webserver.h)、[webserver.cpp](../code/server/webserver.cpp)。
- [build/Makefile](../build/Makefile) 已加入 `../code/config/*.cpp`，测试源码放在子目录中，不会混入服务器构建。

### 实现步骤

1. 定义 `ServerConfig`，内部包含监听、数据库、TLS、日志等子结构，保留当前参数作为初始默认值。
2. 文本和路径使用 `std::string` 或 `std::filesystem::path` 持有，禁止保存临时字符串的 `c_str()`。
3. 把数字触发模式改为 `enum class TriggerMode { LT, ET };`，分别描述监听和连接模式。
4. 改为 `explicit WebServer(ServerConfig config)`，由服务器持有配置。若其他成员初始化依赖配置，`config_` 应在这些成员之前声明。
5. 先在 `main()` 手动填充这个结构，验证行为不变，再增加文件解析。

目标用法：

```cpp
ServerConfig config;
// 此阶段仍在 main() 中填写本机参数。
WebServer server(std::move(config));
server.Start();
```

第一版配置在启动后保持不变。`config_` 在依赖它的成员之前声明，路径由 `config_.server.documentRoot` 持有，`HttpConn::srcDir` 借用其 `c_str()`，旧的 `getcwd()`、路径拼接和 `free()` 已移除。服务器关闭时与工作线程的存活协调仍属于问题 6。

将来若把日志路径改成可配置字符串，应同步明确日志对象对路径的所有权；当前日志仍使用固定字符串。

### 验收

- [x] 服务器构造函数不再接收十几个位置参数。
- [x] 端口、超时、线程数等默认值有 Loader 回归覆盖。
- [x] 配置字符串离开加载函数后仍然有效。
- [x] 本机密码未作为新的默认值写进公共配置头文件。
- [ ] 使用本轮配置入口重新验收 HTTP/HTTPS 页面和数据库业务。

## 4. 阶段二：加入 TOML 文件读取与校验

状态：已实现。入口为 [configloader.h](../code/config/configloader.h) 中的 `LoadConfig()`，实现见 [configloader.cpp](../code/config/configloader.cpp)。

### 文件与规则

当前使用系统安装的 [toml++](https://github.com/marzer/tomlplusplus) v3.4.0，源码通过 `#include <toml++/toml.hpp>` 引入。项目构建使用 C++23。

构建通过 `pkg-config --cflags tomlplusplus` 和 `pkg-config --libs tomlplusplus` 获取编译与链接参数；本机对应 `-DTOML_HEADER_ONLY=0 -DTOML_SHARED_LIB=1` 和 `-ltomlplusplus`。已不依赖先前的 `third_party/tomlplusplus` 单头文件目录。

构建前可执行 `pkg-config --modversion tomlplusplus` 检查依赖。VS Code 使用 `/usr/bin/g++` 和 C++23，系统头文件由编译器搜索路径提供。

可提交的配置模板为 [config/server.example.toml](../config/server.example.toml)，本机使用 `config/server.toml`，后者已加入忽略规则。`.conf` 只是扩展名，本项目使用 `.toml` 明确表示格式。

以下只列出当前已经支持的字段，具体内容以仓库中的配置模板为准：

```toml
[server]
port = 8888
listenMode = "et"
connectionMode = "et"
idleTimeoutMs = 60000
linger = false
workerThreads = 6
documentRoot = "../rss"

[database]
host = "localhost"
port = 3306
user = "your_user"
password = "replace_with_local_password"
name = "yourdb"
poolSize = 12

[tls]
enabled = false
certificate = "../ssl/lan-server.crt"
privateKey = "../ssl/lan-server.key"
clientCaFile = ""

[logging]
enabled = true
level = 1
asyncQueueSize = 1024
```

约定：

- 命令行支持 `--help` 和 `--config <文件>`；不带参数时读取 `config/server.toml`。
- 配置文件内部的相对路径统一相对于配置文件所在目录，读取后转换成明确的路径。上例假设文件在 `config/` 下。
- 命令行中的配置文件路径仍按通常规则相对于启动目录；跨目录启动时使用绝对配置路径。
- 缺失的可选字段才使用默认值。字段类型错误、未知字段、非法枚举和溢出应报错，避免拼错字段后静默使用默认值。
- `ReadInteger()` 先读取 `int64_t`，检查 `int` 范围后再转换；布尔值和字符串采用严格类型读取。
- `ResolvePath()` 使用 `weakly_canonical()` 整理路径，随后独立检查目录或普通文件类型；TLS 关闭时允许证书路径为空或目标尚不存在。
- TOML 解析错误包含配置文件、行列及原因；字段校验错误包含配置文件、字段和原因。`main()` 捕获异常并通过 `stderr` 输出，此时日志系统可能尚未启动。

至少检查：

| 参数 | 第一版检查规则 |
| --- | --- |
| 监听端口 | 先沿用现有 1024～65535 限制；如需其他范围，再同步修改监听检查 |
| 数据库端口 | 1～65535 |
| 工作线程数、数据库连接池大小 | 正数，并限制在 `int` 范围内；实际可分配资源仍取决于运行环境 |
| 空闲超时 | 非负，明确约定 0 表示关闭空闲定时器 |
| 日志队列容量 | 非负，0 表示同步日志，正数表示异步队列容量 |
| 日志级别 | 整数 0～3，分别为 DEBUG、INFO、WARN、ERROR；当前阈值比较方向待阶段四修正 |
| 数据库文本 | 主机、用户、库名非空；密码允许为空；文本拒绝嵌入 NUL |
| 资源目录 | 必须是已存在的目录 |
| 证书和私钥 | TLS 开启时必须是已存在的普通文件，客户端 CA 非空时也检查；内容有效性与私钥匹配由 TLS 初始化继续检查 |

当前 `clientCaFile` 非空会开启强制客户端证书验证。普通手机、浏览器的单向 HTTPS 使用空字符串；独立的 `requireClientCertificate` 开关留到阶段四实现。

当前日志路径仍相对于启动工作目录使用 `./log`，不属于 Loader 的路径解析范围。

### 构建与使用

首次使用时复制模板，并编辑本机数据库参数；已有本机配置时直接编辑原文件：

```bash
cp config/server.example.toml config/server.toml
make
bin/server --config config/server.toml
```

修改配置后重新启动服务器即可生效；当前不支持运行中重载。覆盖可执行文件前先停止旧进程。

独立运行配置回归：

```bash
make config-test
```

[configloaderTest.cpp](../code/config/test/configloaderTest.cpp) 自动准备和清理临时目录，不依赖 MySQL 服务或真实证书。42 项检查覆盖字段与默认值、类型和数值范围、未知字段、模式、路径基准、文件类型、NUL、TOML 语法及缺失文件。测试中的证书文件只是类型检查占位文件，不证明 TLS 内容有效。

本轮还通过了完整服务器编译与链接，以及 `--help`、非法命令行参数、缺失配置文件的启动检查。真实 MySQL、HTTP/HTTPS 服务与模块行为留在阶段四继续验收。

### 验收

- [x] Loader 读取端口、资源目录和线程数等字段，`main()` 将结果传给服务器。
- [x] 不同工作目录下读取同一配置，资源和 TLS 路径保持一致；由 Loader 测试覆盖。
- [x] 缺少文件、语法错误、错误类型、未知字段、非法模式、负数或溢出均被拒绝；Loader 回归通过。
- [x] `main()` 将配置异常输出到 `stderr` 并返回非零退出码。
- [x] 示例配置使用占位值；本机配置已加入忽略规则。
- [ ] 使用真实依赖启动，验证修改配置后端口、资源和线程数的实际行为。

## 5. 阶段三：支持环境变量和可选 .env

状态：待实现。环境变量支持可以独立提交，`.env` 是可选的后续便利功能。

TOML 保存结构化配置，环境变量用于启动时注入部署参数，`.env` 则是本地保存这些变量的文本文件。分开并非技术要求，当前也可继续在被忽略的本机 TOML 中保存密码。两种文件都是明文，`.env` 不提供加密。

### 实现步骤

1. 为 Loader 增加 `database.passwordEnv` 字段及白名单，通过 `std::getenv()` 读取指定变量。复制读取结果到 `config.database.password`，不长期保存环境字符串指针。TOML 解析器不会自动替换环境变量。
2. 约定 `password` 与 `passwordEnv` 不能同时填写；指定变量不存在时明确报错。是否允许变量值为空需要单独约定，不回退为源码中的旧密码。
3. 再按需要增加 `--env-file <文件>`。它应由程序显式加载，`getenv()` 本身不会读取 `.env`。
4. 将第一版 `.env` 语法限制为清楚的键值、空行和注释，不执行文件里的 shell 命令。若使用现成解析库，记录其引号、转义规则。
5. 约定进程环境变量优先于 `.env` 中的同名值，方便部署时覆盖本机默认值。
6. 在最终 `Validate()` 之前完成来源选择与值合并，再初始化服务器；第一版只在启动时读取。

待实现的 TOML 用法：

```toml
[database]
host = "localhost"
user = "your_user"
name = "yourdb"
passwordEnv = "TINYWEB_DB_PASSWORD"
```

`.env.example` 只保存占位值：

```dotenv
TINYWEB_DB_PASSWORD=replace_with_local_password
```

阶段完成后的预期命令：

```bash
bin/server --config config/server.toml --env-file .env
```

上述命令在命令行解析实现后才可使用。两个命令行文件路径都相对于启动目录；它们与 TOML 内部路径的基准要分别处理。

当前 [.gitignore](../.gitignore) 已忽略 `.env`、`.env.*`，保留 `.env.example` 等示例。`.env` 是本机明文配置，不是加密存储；日志、异常信息和配置摘要均不输出密码。

### 验收

- [ ] 同名进程环境变量能够覆盖 `.env` 中的值。
- [ ] 必需变量缺失、指定的环境文件不存在或格式不合法时，有明确错误。
- [ ] 配置摘要只显示数据库主机、库名等非秘密信息。
- [ ] `git diff` 中没有真实密码、证书或私钥。

## 6. 阶段四：接入各模块，并修正配置与实际行为的差异

状态：部分接入。端口、模式、超时、资源路径、线程数以及数据库和 TLS 参数已使用 `config_`，下面集中记录已完成的接入和仍待修正的行为。

启动流程建议为：读取文件 → 合并环境值 → 校验并解析路径 → 初始化日志 → 初始化数据库、TLS 等依赖 → 准备监听和工作线程 → 进入事件循环。信号接入时还需在任何线程创建之前设置信号屏蔽，见第 10 节。

这里需要让初始化失败可向上层返回，不能只打印错误后继续启动。

| 修改位置 | 当前状态与剩余工作 |
| --- | --- |
| `WebServer::initSocket_()` | 已读取配置端口和 linger；监听地址、backlog、连接上限仍固定，后续添加对应字段时再接入 |
| `WebServer` 构造与资源目录 | 已使用 Loader 解析后的路径并移除旧的 `getcwd()`、拼接和 `free()`；工作线程与配置存储的关闭协调仍按问题 6 安排 |
| `SqlConnPool::Init()` | 已接收配置主机与其他参数；待增加可检查的初始化结果，连接失败时不能把空指针当作可用连接入池，并清理失败资源 |
| `Log::init()` | 在其他模块需要日志前初始化；把 `maxQueueCapacity` 真正传给 `BlockDeque`，轮转条件使用配置值 |
| `LOG_BASE` | 若把级别定义为最低输出阈值，应使用 `消息级别 >= 配置阈值`；当前比较方向相反 |
| `SSLContext` 初始化 | TLS 开关和路径已接入；待将客户端 CA 与是否要求客户端证书分开处理 |
| 初始化失败上报 | 当前 TLS 或监听失败会设置关闭标志；待让启动失败明确返回非零退出码，并保证关闭日志时也能看到原因 |
| `HttpResponse::AddHeader_()` | 当前固定输出 `max=6, timeout=120`，但没有对应次数限制，空闲超时已可配置；删除无依据的参数，或补齐真实行为后再输出 |

这里的“提前初始化日志”包括调整构造过程：当前线程池在成员初始化列表中创建，仅移动构造函数体内的日志代码并不能改变线程的创建时间。

当前配置只在启动时读取。以后增加的监听地址、日志输出目录等字段也先作为启动参数；不要收到重载请求就逐项修改正在使用的对象。

### 验收

- [ ] 数据库、TLS、监听初始化失败时，不进入正常服务循环；错误能够被看到。
- [ ] 日志级别为 `info` 时输出 INFO/WARN/ERROR，隐藏 DEBUG；队列容量与配置一致。
- [ ] HTTP 和 HTTPS 页面访问、文件响应回归仍通过。
- [ ] 保持请求解析行为；无可用数据库时登录验证不会因空连接断言退出。
- [ ] 先停止旧进程再覆盖其可执行文件；本阶段仍采用重启生效。

既有回归的编译方式见 [HTTP / HTTPS 测试说明](../code/http/test/readme.md)。数据库无连接回归只证明失败路径，不代表正确账号下的登录注册已经通过。

## 7. 阶段五：先分离业务，再考虑动态库

### 修改位置与边界

目前 `HttpRequest::ParsePost_()` 会调用 `UserVerify()`，并把路径改为欢迎页或错误页；`HttpConn::process()` 解析后直接构造文件响应。

建议先新增普通的业务处理层，例如 `code/handler/`，暂时仍静态编译到主程序：

```text
HttpRequest 解析请求数据
    → 业务分派器选择处理逻辑
    → 返回业务结果
    → 主程序生成 HTTP 响应并发送
```

- 解析器只处理请求行、头、请求体以及表单解码，不执行登录注册、访问数据库或选择业务页面。
- 业务层决定登录结果、响应状态、正文或资源路径。
- 主程序继续管理 HTTP 头、连接关闭、发送缓冲区、资源文件打开和映射。
- 业务层返回资源路径时，仍须经过现有资源根目录约束，不能绕过安全打开逻辑。

先新增不依赖数据库的 `/api/version`，返回固定文本。第一版插件只迁移这个接口；登录注册先留在主程序业务层，等数据库等主程序服务的调用接口设计清楚后再迁移。

### 验收

- [ ] 仅解析 POST 表单不会触发 SQL 操作。
- [ ] 分段输入、连续请求、非法头等解析回归不依赖业务层。
- [ ] `/api/version` 能返回固定内容，原静态资源和业务路由保持预期行为。
- [ ] 明确每种业务结果的状态码、正文、资源路径由哪一层处理。

## 8. 阶段六：定义插件 ABI，先实现启动加载

### 建议文件

- `code/plugin/plugin_api.h`：主程序与插件共享的接口。
- `code/plugin/module.h/.cpp`：一个已加载模块的 RAII 包装。
- `code/plugin/pluginmanager.h/.cpp`：加载与当前版本管理。
- `plugins/business.cpp`：首个插件实现，与主程序分开构建。

第一版先在启动时加载一个插件，运行期间不切换，确认调用链和资源所有权正确。

### 接口示意

以下是可继续扩充的接口草案，缓冲区都通过指针和长度描述。主程序和插件需采用兼容的目标平台与 ABI 构建约定。

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* path;
    size_t path_size;
} TinywebRequestView;

typedef struct {
    void* user;
    int (*append)(void* user, const char* data, size_t size);
} TinywebResponseWriter;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*create)(void** instance);
    int (*handle)(void* instance, const TinywebRequestView* request,
                  const TinywebResponseWriter* writer);
    void (*destroy)(void* instance);
} TinywebPluginApi;

__attribute__((visibility("default")))
const TinywebPluginApi* tinyweb_plugin_get_api(void);

#ifdef __cplusplus
}
#endif
```

约定第一版 ABI 为 1，`struct_size` 填 `sizeof(TinywebPluginApi)`；实际返回的函数表在模块加载期间保持有效。业务接口成熟后再添加 method、请求体、响应状态和主程序服务接口，并制定相应的 ABI 兼容规则。

必须明确的调用规则：

- `extern "C"` 固定入口的符号名称，不会自动让跨库传递 C++ 类变得稳定。
- 不在接口中直接传递 `std::string`、容器、`HttpConn` 或异常；插件内部可以使用 C++。
- `create`、`handle` 返回 0 表示成功，非零表示失败；插件内部捕获异常，转换成错误码。
- `create` 失败时把实例输出置空并清理半成品；成功后必须有匹配的 `destroy`，由创建它的模块销毁实例。
- 请求数据只在本次调用期间有效；`append` 由主程序提供，并在返回前复制数据。插件不能保存请求指针或 writer 留待以后使用。
- 函数可以被不同工作线程并发调用，插件状态必须只读或自行同步。
- 第一版不允许插件创建后台线程或保存异步回调。长期数据留在主程序或数据库中。
- 若增加响应头接口，主程序仍校验状态和字段合法性、负责消息长度，不能让插件直接拼接不受控的 HTTP 头。

### 加载与释放

`Module::Load()` 按顺序执行：

1. `dlopen(path, RTLD_NOW | RTLD_LOCAL)`。
2. 先清理旧 `dlerror()`，再 `dlsym()` 查找 `tinyweb_plugin_get_api`，用新的 `dlerror()` 判断错误。
3. 检查入口、ABI 版本、结构体大小和必需函数指针。
4. 调用 `create()`，成功后才返回可用模块。

加载过程使用 RAII 覆盖失败路径。模块析构先调用 `destroy()`，再调用 `dlclose()`，不能卸载后再跳转到插件的销毁函数。

实现文件准备好后，插件构建命令示意如下；`v1` 文件只在尚未加载时创建：

```bash
g++ -std=c++23 -O2 -fPIC -shared -fvisibility=hidden \
  -Icode/plugin plugins/business.cpp -o plugins/business.v1.so
```

主程序链接增加 `-ldl`，并把新的配置、业务层、插件管理源码加入构建列表。插件源码不应同时编入主程序。隐藏默认符号时，共享头中的入口导出声明也要用于插件的函数定义。

### 验收

- [ ] 正确插件可被加载，`/api/version` 能返回 v1。
- [ ] 缺少文件、缺少入口、错误 ABI、初始化失败都有明确错误，且不会泄漏成功打开的句柄。
- [ ] 停止使用插件后，实例先销毁，再关闭库句柄。
- [ ] 插件返回的正文已复制进主程序存储，发送时不依赖插件的临时字符串。

## 9. 阶段七：实现版本切换和旧版本延迟释放

### 核心结构

本项目采用 C++23，可以使用 C++20 提供的 `atomic<shared_ptr<T>>`：

```cpp
std::atomic<std::shared_ptr<Module>> current_;
std::mutex reloadMutex_;
```

重载过程只发布完全可用的新模块：

```cpp
// 伪代码：错误需要记录，多个重载操作需要串行化。
auto candidate = Module::Load(newPath);
current_.store(std::move(candidate), std::memory_order_release);
```

业务派发时取得一个版本快照：

```cpp
auto module = current_.load(std::memory_order_acquire);
// 检查空值后，使用 module 处理本次业务。
```

约定版本选择点为业务派发时。同一请求的全部插件调用使用同一个快照，不能处理一半又重新读取当前版本。若把调用交给异步任务，任务本身必须捕获这个强引用。

切换示例：

| 时刻 | 当前版本 | 请求 A | 请求 B |
| --- | --- | --- | --- |
| T1 | v1 | 取得 v1 引用并开始处理 | 尚未派发 |
| T2 | 发布 v2 | 继续使用 v1 | 尚未派发 |
| T3 | v2 | 完成后释放 v1 引用 | 取得 v2 引用并处理 |

最后一个使用者释放旧模块后，RAII 执行销毁和 `dlclose()`。如果响应已经完整复制进主程序缓冲区，后续网络发送不必继续持有插件。库是否实际从地址空间移除还取决于动态加载器及其他引用，不能把 `dlclose()` 等同于强制卸载。

### 发布与失败处理

- 每次构建使用新文件名，例如 `business.v2.so`；不要覆盖已加载文件，也不要认为对同一已加载对象再次 `dlopen()` 一定得到新代码。
- 先在临时产物上完成链接与检查，再以新版本名发布，避免加载写入中的半成品。
- 加载、ABI 校验、初始化失败时不修改 `current_`，继续服务原版本，并记录失败原因。
- 重载操作用互斥锁串行化；仅原子地替换指针还不足以定义多个同时重载的先后顺序。
- 不在 epoll 主循环里执行耗时的编译或初始化；由明确的重载执行者准备候选版本。
- 插件处理请求失败时按错误响应策略处理。不要自动用旧版重试登录注册等可能已产生数据库副作用的请求。
- 同进程 `.so` 没有进程隔离，插件崩溃仍可能带走服务器；版本校验不能替代代码正确性。

### 验收

- [ ] 请求 A 持有 v1 时发布 v2，新业务调用得到 v2，A 仍使用 v1 完成。
- [ ] A 未完成时，v1 的实例和库句柄保持有效；A 释放后才允许清理。
- [ ] 缺少文件、ABI 错误、初始化失败均不替换当前版本。
- [ ] 多次重载和同时发起的重载有确定顺序；不存在最后使用者仍执行代码时提前卸载。
- [ ] 测试不只检查输出字符串，还记录模块销毁时机；已有临时原型不能替代接入真实请求后的验收。

此前独立临时原型通过了 7 项检查：初始 v1、新请求使用 v2、旧请求保留 v1、ABI 错误保留 v2、缺失库保留 v2、旧请求继续完成、旧请求结束后模块引用释放。该原型尚未入库，本文不依赖会被清理的 `/tmp` 路径；初始化失败和真实服务器接入仍需补测。

## 10. 阶段八：增加重载触发，再选择可重载配置

建议先通过测试程序或管理入口显式调用 `Reload(newPath)`，前一阶段稳定后再接信号。

本项目使用 Linux epoll，可采用 `SIGHUP + signalfd`：

1. 在 `main()` 中使用 `pthread_sigmask()` 屏蔽 SIGHUP，必须早于任何线程创建，包括日志异步线程和工作线程。
2. 创建非阻塞、关闭时不继承的 signalfd，将其加入 epoll。
3. 在 `Start()` 中先识别 signalfd，再进入客户端事件分派；它不是 `user_` 中的连接，不能落入连接断言。
4. 收到事件后读取信号，向重载执行者提交任务；不要在异步信号处理函数中调用 `dlopen()`、日志或复杂对象操作。
5. 明确该次重载选择哪个版本文件，可从重新读取的配置中获得。标准信号可能合并，重载应按最新期望状态执行，而不是依赖信号计数。

完成上述接入后才使用 `kill -HUP <服务器PID>`。当前代码尚未处理 SIGHUP，直接发送可能终止进程。

若同时实现配置重载，先完整解析、校验，再发布配置快照，不能边读边修改正在服务的对象。建议按字段划分：

| 配置项 | 重载策略 |
| --- | --- |
| 插件路径 | 加载成功后切换；失败保留原插件 |
| 日志最低级别 | 改为原子值或受锁保护后可重载 |
| 监听地址、端口、ET/LT 模式 | 第一版要求重新启动 |
| 工作线程数、SQL 连接池、日志队列或目录 | 第一版要求重新启动 |
| 资源根目录、请求大小限制 | 以后按请求取得一致快照，再设计重载 |
| TLS 证书和私钥 | 独立按下一节实施 |

对暂不支持重载的字段，明确拒绝该次更新或提示需要重启，不能静默忽略。若一次操作包含多个可重载部分，要明确哪些作为一个整体发布；第一版可以只支持插件路径，避免部分成功、部分失败的语义。

### 验收

- [ ] 信号事件不会被当成客户端连接处理；信号到来不会直接终止服务器。
- [ ] 失败的重载保留当前插件和有效配置。
- [ ] 重载期间普通请求继续处理，日志记录目标版本及成功或失败原因，不输出秘密。
- [ ] 停止重载执行者时有明确的任务退出与依赖存活规则；并发关闭仍按问题 6 单独验收。

## 11. 可选阶段：证书热加载

当前 `SSLContext::Initialize()` 在 `initialized_` 已置位后返回失败，不能直接再次调用它实现证书更新。

后续可以采用独立的 TLS 上下文管理器：

1. 新建一个 `SSL_CTX`，加载完整的证书、私钥和验证选项。
2. 检查证书加载结果、私钥匹配等；失败时保留当前上下文。
3. 原子发布新的上下文，新连接创建 `SSL*` 时取得一致的上下文快照。
4. 已有连接继续使用原来的 `SSL*` 和上下文，不在握手中途替换。
5. 用明确的引用和所有权保证旧上下文仍可被已有连接使用，等待使用结束后释放。

配置路径变更、磁盘上的证书被替换，都不会自动修改现有 TLS 会话。证书热加载可以独立于业务 `.so` 更新实现，也不等于修复了连接关闭时的 SSL 资源竞争。

验收至少覆盖新连接看到新证书、旧连接不中断、错误证书或不匹配私钥不会替换当前上下文，以及并发创建连接时的生命周期。

## 12. 推荐实施与提交顺序

按完整且可验证的改动提交，不强制“一阶段对应一个 commit”。每个提交应能独立构建并通过相关检查；如果配置结构与 Loader 已经一起接入，可以合并为一个提交：

```text
feat(config): load and validate server settings from TOML
```

以下仍可作为后续拆分参考：

1. `refactor(config): introduce owned server configuration`：配置结构替代位置参数。
2. `feat(config): load and validate TOML configuration`：文件解析、路径规则与参数检查。
3. `feat(config): support database credentials from environment`：环境变量与可选 `.env`。
4. `fix(config): apply settings consistently across server modules`：日志、数据库、路径和响应头的实际接入。
5. `refactor(http): separate request parsing from business handlers`：业务层与 `/api/version`。
6. `feat(plugin): load business handlers through a versioned C ABI`：启动加载与释放。
7. `feat(plugin): switch module versions while retaining active handlers`：版本快照与延迟释放。
8. `feat(server): trigger plugin reload through signalfd`：明确的重载触发。

当前先收尾并提交阶段一、二，随后实现阶段三的环境变量读取，按需要增加 `.env`，再处理阶段四的实际行为差异。业务插件与热更新留到这些启动行为明确之后。

提交前执行 `make config-test`、完整构建和 `git diff HEAD --check`，并检查暂存区内容。仓库保留 `.vscode/c_cpp_properties.json` 和 `settings.json` 这两份共享配置，其他 VS Code 本地配置忽略；本机 TOML、`.env`、私钥、日志和构建产物不提交。

## 参考资料

- [toml++](https://github.com/marzer/tomlplusplus)：TOML 解析库与使用示例。
- [dlopen / dlclose](https://man7.org/linux/man-pages/man3/dlopen.3.html)：加载选项、句柄引用与卸载规则。
- [dlsym](https://man7.org/linux/man-pages/man3/dlsym.3.html)：入口查找与 `dlerror()` 检查。
- [GCC Code Generation Options](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)：`-fPIC` 和符号可见性。
- [signalfd](https://man7.org/linux/man-pages/man2/signalfd.2.html)：通过 fd 接收信号并接入 epoll。
- [pthread_sigmask](https://man7.org/linux/man-pages/man3/pthread_sigmask.3.html)：多线程信号屏蔽及新线程的继承规则。
- [OpenSSL SSL_new](https://docs.openssl.org/3.0/man3/SSL_new/)：从 TLS 上下文创建连接对象。
