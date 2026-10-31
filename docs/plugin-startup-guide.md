# .so 启动加载分步实践

首次记录:2026-10-28.最近同步:2026-10-30.

本文对应 [配置化与热更新指南](config-and-hot-reload-guide.md) 的阶段六.共享头、business.cpp、Module 和独立加载程序已落地,26 项模块回归通过.服务器的配置和请求链尚未接入插件,第 6～9 步仍为后续操作.模块说明见 [plugin 模块](../code/plugin/readme.md).

本阶段的完成标准:主程序启动时加载一个 `.so`,GET `/api/version` 调用插件,正文仍由主程序缓冲区持有.运行期间修改配置或编译另一个版本,当前进程都不会自动切换;版本切换属于阶段七.

## 1. 先确定边界和文件

```text
浏览器
  → HttpRequest 解析
  → RequestHandler 匹配 GET /api/version
  → Module 调用插件 handle
  → 主程序提供的 append 回调复制正文
  → HandlerResult / HttpResponse 生成 HTTP 响应
```

主程序继续管理连接、TLS、线程池、HTTP 头和数据库连接池.第一个插件只返回版本文本.现有登录注册继续使用 UserService.

准备这些文件:

| 文件 | 内容 |
| --- | --- |
| `code/plugin/plugin_api.h` | 主程序与插件共同使用的 C 接口 |
| `plugins/business.cpp` | 插件实现,单独编译 |
| `code/plugin/module.h/.cpp` | 库句柄、函数表、插件实例的 RAII 包装 |
| `code/plugin/test/pluginSmoke.cpp` | 独立加载程序,不启动服务器 |

在项目根目录创建目录:

```bash
mkdir -p code/plugin/test plugins
```

先完成第 2～5 步.这些文件不需要数据库、证书或监听端口即可验证.通过后再做第 6～9 步的服务器集成.本阶段一个 Module 就够用,PluginManager 留到运行时版本管理时引入.

## 2. 定义共享接口

新建 `code/plugin/plugin_api.h`.主程序和插件都包含同一份头文件.

<!-- example-file: code/plugin/plugin_api.h -->

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>

#define TINYWEB_PLUGIN_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* path;
    size_t pathSize;
} TinywebRequestView;

typedef struct {
    void* user;
    int (*append)(void* user, const char* data, size_t size);
} TinywebResponseWriter;

typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
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

这几个字段的含义:

- `tinyweb_plugin_get_api`:固定的导出入口,加载后取得函数表.
- `abiVersion`:主程序与插件约定的二进制接口版本.它与业务文本中的 v1、v2 无关.
- `structSize`:函数表的大小.此入门实现要求版本和大小都精确相等.
- `create / destroy`:创建与销毁插件实例.实例在插件里分配,也由插件销毁.
- `handle`:处理一次调用,通过 writer 返回正文.
- `writer.user`:主程序自己的上下文指针,插件原样传给 append.

`extern "C"` 让入口使用 C 语言链接约定,便于按固定符号名查找.它本身不解决类布局、内存归属和异常传递问题.这里用指针、长度、整数和函数指针描述边界,插件内部仍然可以使用 C++.

第一版约定:

1. 主程序与插件使用兼容的 Linux 目标平台、架构和 ABI 构建约定.
2. `create`、`handle`、`append` 返回 0 表示成功,非零表示失败.
3. `create` 成功时输出非空实例;失败时清理半成品,输出空指针.
4. 插件函数和主程序回调不向接口外抛出异常.
5. request、writer 和正文参数只在本次调用内有效,插件不保存它们.
6. append 在返回前复制正文.多个工作线程可以同时调用 handle,实例中的状态应只读或自行同步.
7. 第一版不创建插件后台线程,也不保存异步回调.

请求结构暂时只有路径,因为主程序会先匹配 GET `/api/version`.这个接口还不支持通用业务请求、状态码或自定义响应头.将来扩展结构体时,需要同时更新版本规则、加载校验和测试;当前实现下修改布局就应升级 ABI 版本.

## 3. 写首个业务插件

新建 `plugins/business.cpp`:

<!-- example-file: plugins/business.cpp -->

```cpp
#include "plugin_api.h"

#include <string>
#include <string_view>

using namespace std;

namespace {

struct BusinessState {
    string version = "TinyWebServer v1\n";
};

int Create(void** instance) noexcept {
    if (instance == nullptr) {
        return -1;
    }
    *instance = nullptr;
    try {
        *instance = new BusinessState;
        return 0;
    } catch (...) {
        return -1;
    }
}

int Handle(void* instance, const TinywebRequestView* request,
           const TinywebResponseWriter* writer) noexcept {
    try {
        if (instance == nullptr || request == nullptr || request->path == nullptr ||
            writer == nullptr || writer->append == nullptr) {
            return -1;
        }

        const string_view path(request->path, request->pathSize);
        if (path != "/api/version") {
            return -1;
        }

        const auto& state = *static_cast<const BusinessState*>(instance);
        return writer->append(writer->user, state.version.data(), state.version.size());
    } catch (...) {
        return -1;
    }
}

void Destroy(void* instance) noexcept {
    delete static_cast<BusinessState*>(instance);
}

} //namespace

extern "C" __attribute__((visibility("default")))
const TinywebPluginApi* tinyweb_plugin_get_api(void) {
    static const TinywebPluginApi api = {
        TINYWEB_PLUGIN_ABI_VERSION,
        sizeof(TinywebPluginApi),
        Create,
        Handle,
        Destroy
    };
    return &api;
}
```

函数表是静态对象,在模块加载期间有效.BusinessState 中的字符串由插件持有;主程序通过 append 得到副本.这个实例创建后不修改,不同工作线程可以同时读取.

在项目根目录首次编译:

```bash
g++ -std=c++23 -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden \
    -Icode/plugin plugins/business.cpp -o plugins/business.v1.so

file plugins/business.v1.so
nm -D --defined-only plugins/business.v1.so
```

应生成 ELF 共享库,并能在导出符号中看到 `tinyweb_plugin_get_api`.动态符号表可能还有工具链生成的符号,无需要求只有这一项.

- `-fPIC`:生成适用于共享库的位置无关代码.
- `-shared`:生成共享库.
- `-fvisibility=hidden`:默认隐藏符号,入口用 visibility(default) 显式导出.

参数含义见 [GCC 代码生成选项](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html) 和 [链接选项](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html).

上面的输出命令用于首次创建、尚未加载的 v1.之后修改版本正文时,使用 `business.v2.so` 等新文件名.不要直接编译覆盖仍被加载的文件.正式加入 Makefile 时应保留临时文件编译成功后再发布的方式.

## 4. 封装 Module

Module 同时持有三个相关资源:

```text
handle_    dlopen 返回的库句柄
api_       指向库内函数表
instance_  create 创建的插件实例
```

函数表和销毁函数都在库里,因此释放顺序必须是先 destroy,再 dlclose.这里返回 shared_ptr,方便服务器和请求处理代码共同持有模块.它只管理模块的所有权,不会自动保证可变业务状态的线程安全.

### code/plugin/module.h

<!-- example-file: code/plugin/module.h -->

```cpp
#pragma once

#include "plugin_api.h"

#include <memory>
#include <string>

class Module {
public:
    static std::shared_ptr<Module> Load(const std::string& path);
    ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    std::string Handle(const std::string& path) const;

private:
    Module() = default;

    void* handle_ = nullptr;
    const TinywebPluginApi* api_ = nullptr;
    void* instance_ = nullptr;
};
```

### code/plugin/module.cpp

<!-- example-file: code/plugin/module.cpp -->

```cpp
#include "module.h"

#include <dlfcn.h>
#include <stdexcept>
#include <utility>

using namespace std;

namespace {

//入门插件只返回短文本,本例限制单次响应正文为64KiB
constexpr size_t maxBodySize = 64 * 1024;

struct ResponseBuffer {
    string body;
    bool failed = false;
};

int Append(void* user, const char* data, size_t size) noexcept {
    if (user == nullptr) {
        return -1;
    }
    auto& buffer = *static_cast<ResponseBuffer*>(user);
    if (buffer.failed) {
        return -1;
    }
    if ((data == nullptr && size != 0) || size > maxBodySize - buffer.body.size()) {
        buffer.failed = true;
        return -1;
    }
    try {
        if (size != 0) {
            buffer.body.append(data, size);
        }
        return 0;
    } catch (...) {
        buffer.failed = true;
        return -1;
    }
}

} //namespace

shared_ptr<Module> Module::Load(const string& path) {
    if (path.empty() || path.find('\0') != string::npos) {
        throw invalid_argument("Plugin path must be nonempty and contain no NUL");
    }

    //先建立资源持有者,后续任一步抛异常都会回收已取得的资源
    auto module = shared_ptr<Module>(new Module);
    module->handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module->handle_ == nullptr) {
        const char* error = dlerror();
        throw runtime_error("dlopen failed: " + string(error == nullptr ? "unknown error" : error));
    }

    dlerror();
    void* symbol = dlsym(module->handle_, "tinyweb_plugin_get_api");
    const char* error = dlerror();
    if (error != nullptr) {
        throw runtime_error("dlsym failed: " + string(error));
    }
    if (symbol == nullptr) {
        throw runtime_error("Plugin entry is null");
    }

    //这里使用Linux/POSIX动态加载接口提供的函数地址转换约定
    using GetApi = const TinywebPluginApi* (*)(void);
    const auto getApi = reinterpret_cast<GetApi>(symbol);
    module->api_ = getApi();
    if (module->api_ == nullptr) {
        throw runtime_error("Plugin API is null");
    }
    if (module->api_->abiVersion != TINYWEB_PLUGIN_ABI_VERSION ||
        module->api_->structSize != sizeof(TinywebPluginApi)) {
        throw runtime_error("Plugin ABI version or structure size mismatch");
    }
    if (module->api_->create == nullptr || module->api_->handle == nullptr ||
        module->api_->destroy == nullptr) {
        throw runtime_error("Plugin API has missing functions");
    }

    const int result = module->api_->create(&module->instance_);
    if (result != 0 || module->instance_ == nullptr) {
        throw runtime_error("Plugin create failed");
    }
    return module;
}

Module::~Module() {
    if (instance_ != nullptr) {
        api_->destroy(instance_);
    }
    if (handle_ != nullptr) {
        dlclose(handle_);
    }
}

string Module::Handle(const string& path) const {
    ResponseBuffer buffer;
    const TinywebRequestView request = {path.data(), path.size()};
    const TinywebResponseWriter writer = {&buffer, Append};
    const int result = api_->handle(instance_, &request, &writer);
    if (result != 0 || buffer.failed) {
        throw runtime_error("Plugin handle failed");
    }
    return std::move(buffer.body);
}
```

加载顺序为:

1. `dlopen(..., RTLD_NOW | RTLD_LOCAL)` 加载库并解析依赖符号.
2. 清除旧 dlerror,再 dlsym 查入口,之后检查新的 dlerror.
3. 取得函数表,校验 ABI、大小和必需函数.
4. 创建实例,全部成功后返回 Module.

RTLD_NOW 让未解析的符号在加载时暴露,RTLD_LOCAL 不把插件符号发布给随后加载的库作普通符号解析使用.具体加载行为见 [dlopen 手册](https://man7.org/linux/man-pages/man3/dlopen.3.html).dlsym 前后处理 dlerror 的方式见 [dlsym 手册](https://man7.org/linux/man-pages/man3/dlsym.3.html).

append 的 failed 标志会记住失败,即使插件忽略回调错误,主程序仍拒绝这次结果.64KiB 是这个短文本示例的约定,不是整个服务器的文件响应限制.

## 5. 独立加载验证

新建 `code/plugin/test/pluginSmoke.cpp`:

<!-- example-file: code/plugin/test/pluginSmoke.cpp -->

```cpp
#include "../module.h"

#include <exception>
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: pluginSmoke PATH_TO_PLUGIN\n";
        return 1;
    }
    try {
        string body;
        {
            const auto module = Module::Load(argv[1]);
            body = module->Handle("/api/version");
        }
        //Module已经释放,输出仍使用主程序持有的正文
        cout << body;
        return 0;
    } catch (const exception& error) {
        cerr << error.what() << '\n';
        return 1;
    }
}
```

在项目根目录编译并运行:

```bash
g++ -std=c++23 -O2 -Wall -Wextra \
    code/plugin/module.cpp code/plugin/test/pluginSmoke.cpp \
    -o code/plugin/test/pluginSmoke -ldl

./code/plugin/test/pluginSmoke ./plugins/business.v1.so
```

预期输出:

```text
TinyWebServer v1
```

再验证文件缺失:

```bash
./code/plugin/test/pluginSmoke ./plugins/missing.so
echo $?
```

应该在 stderr 输出 dlopen 错误,退出码为 1.传入带 `/` 的相对路径或绝对路径,便于明确加载哪个文件.主程序不需要在链接时加 `-lbusiness`,也不必将插件安装到系统目录.

还应补充:没有入口的库、错误 ABI、错误大小、空函数表、缺失函数、create 失败、handle 失败、append 超限.测试源码继续放在 `code/plugin/test/`,配自己的 Makefile 和 readme.后续可用桩插件分别制造这些结果.

到这里可以先暂停检查.独立程序能加载插件之后,再继续连接服务器.

## 6. 增加插件配置

以下是服务器集成步骤,不是上面独立示例自动完成的功能.目前 Loader 使用严格白名单,直接给本地 TOML 加 `[plugin]` 会被当作未知字段拒绝,需要先修改配置代码.

### 6.1 serverconfig.h

新增配置结构,并在 ServerConfig 中增加 `PluginConfig plugin;`:

```cpp
struct PluginConfig {
    bool enabled = false;
    std::string path;
};
```

默认禁用,保留原配置和原路由的行为.

### 6.2 configloader.cpp

按现有读取方式完成下面四处修改:

1. 根白名单加入 `"plugin"`.
2. `Section(root, "plugin")` 取得表,`CheckKeys` 只允许 `enabled` 和 `path`.
3. ReadOptional 读取 bool 和 string,path 调用现有 ResolvePath.
4. Validate 中,启用时 RequireRegularFile,禁用时允许空路径.

核心片段,分别插入读取阶段和 Validate:

```cpp
//读取阶段,plugin变量来自Section(root, "plugin")
config.plugin.enabled = ReadOptional(plugin, "enabled", config.plugin.enabled, "plugin.enabled");
config.plugin.path = ReadOptional(plugin, "path", config.plugin.path, "plugin.path");
ResolvePath(config.plugin.path, base, "plugin.path");
```

```cpp
//Validate内部
if (config.plugin.enabled) {
    RequireRegularFile(config.plugin.path, "plugin.path");
}
```

路径与已有 documentRoot、证书路径一致,以配置文件所在目录为基准.文件存在只是一层检查,是否真能加载仍由 Module::Load 决定.

### 6.3 示例和本地配置

在公开 `config/server.example.toml` 中新增,默认关闭:

```toml
[plugin]
enabled = false
path = "../plugins/business.v1.so"
```

本地 `config/server.toml` 完成代码接入后再设为 true.不要把数据库密码或本机配置加入公开模板.

## 7. 把 Module 传到请求处理层

本阶段可采用下面的持有关系:

```text
WebServer.businessModule_       shared_ptr<Module>
    ↓ AddClient_ 时复制
HttpConn.businessModule_        shared_ptr<Module>
    ↓ process() 构造 handler 时复制
RequestHandler.module_          shared_ptr<Module>
```

HttpConn 是 HttpsConn 的基类,这条处理链可供 HTTP/HTTPS 共同使用.插件不需要调用 SSL_read、SSL_write 或知道连接类型.

### 7.1 WebServer

`webserver.h` 增加 Module 前向声明,包含 `<memory>`,再增加成员:

```cpp
std::shared_ptr<Module> businessModule_;
```

`webserver.cpp` 包含 `../plugin/module.h`.构造函数已有 try 块中,放在 TLS 初始化成功之后、创建监听与工作线程之前:

```cpp
if (config_.plugin.enabled) {
    businessModule_ = Module::Load(config_.plugin.path);
}
```

启用的插件加载失败就抛出异常,沿用已有启动清理和 main 的 stderr/非零退出路径.启动失败不进入服务循环.

### 7.2 HttpConn

头文件包含 `<memory>`,增加 Module 前向声明、私有成员和公开 setter:

```cpp
//private
std::shared_ptr<Module> businessModule_;

//public
void SetBusinessModule(std::shared_ptr<Module> module);
```

在 `.cpp` 包含 `<utility>`,实现:

```cpp
void HttpConn::SetBusinessModule(shared_ptr<Module> module) {
    businessModule_ = std::move(module);
}
```

在 `WebServer::AddClient_` 取得 client 后、Init 和投递任何工作任务前设置:

```cpp
client->SetBusinessModule(businessModule_);
client->Init(fd, addr);
```

赋值发生在连接发布给工作线程之前,运行期间不再修改这个连接的模块指针.

### 7.3 RequestHandler

保留已有的验证函数注入,构造函数增加第二个可选参数.头文件包含 `<memory>`,前向声明 Module:

```cpp
explicit RequestHandler(UserVerifier verifier = UserService::Verify,
                        std::shared_ptr<Module> module = nullptr);
```

新增私有成员:

```cpp
std::shared_ptr<Module> module_;
```

`.cpp` 包含 `../plugin/module.h` 和 `<utility>`,构造函数初始化 verifier_、module_,保留现有空验证函数检查:

```cpp
RequestHandler::RequestHandler(UserVerifier verifier, shared_ptr<Module> module)
    : verifier_(verifier), module_(std::move(module)) {
    if (verifier_ == nullptr) {
        throw invalid_argument("User verifier must not be null");
    }
}
```

`HttpConn::process()` 中原来的无参 handler 改成:

```cpp
const RequestHandler handler(UserService::Verify, businessModule_);
```

现有测试仍可构造无参 handler 或仅传入 FakeVerify,不必全部改成依赖插件.

### 7.4 改造版本路由

在现有 GET `/api/version` 分支中使用模块.该 `.cpp` 按需补 `<exception>` 和 `../log/log.h`:

```cpp
if (request.method() == "GET" && result.path == "/api/version") {
    result.kind = HandlerResult::Kind::Text;
    result.path.clear();
    if (module_ == nullptr) {
        result.body = "TinyWebServer v1\n";
        return result;
    }
    try {
        result.body = module_->Handle(request.path());
    } catch (const exception& error) {
        LOG_ERROR("Plugin request failed: %s", error.what());
        result.status = 500;
        result.body = "Internal Server Error\n";
    }
    return result;
}
```

未启用插件时保留原文本响应.启用后插件执行失败返回 500,正文不包含内部路径和错误详情.预期的模块异常在业务分派层转换为响应,不让它直接逃出工作线程任务.

HandlerResult、HttpResponse 和发送缓冲区仍由主程序管理.模块返回的是主程序中的 string,HTTP 写操作不必继续引用插件里的字符串.

这里的连接级共享引用用于启动时固定模块.阶段七实现切换时,要在每次业务分派时从 PluginManager 取得当前模块快照,否则同一个 keep-alive 连接会一直调用旧版本.原有连接裸指针和线程关闭协调问题仍单独处理.

## 8. 接入构建与测试目录

### 8.1 主程序

修改 `build/Makefile`:

- SOURCES 的 wildcard 加入 `../code/plugin/*.cpp`.
- 链接库追加 `-ldl`.
- 只把插件宿主代码编进 server,`plugins/business.cpp` 单独编成共享库.
- 保留服务器临时目录编译成功后 mv 的现有流程.

凡是测试目标已经编译 HttpConn 或 RequestHandler,增加它依赖的 module.cpp 和 `-ldl`.当前重点检查:

```text
code/http/test/Makefile
code/handler/test/Makefile
code/pool/test/Makefile
code/server/test/Makefile
```

保持纯解析测试不链接数据库或插件宿主源码.配置测试只需覆盖新增字段的读取、默认值和校验.

### 8.2 插件和独立测试

当前已加入:

- `code/plugin/test/Makefile`:在本目录生成并运行测试程序.
- `code/plugin/test/readme.md`:记录运行方式、测试范围和限制.
- 根目录和 build/Makefile 的 `plugin-test`:只转发到模块测试目录.

`plugins/Makefile` 留待后续构建集成时增加,用于指定版本输出,例如 `make -C plugins VERSION=v2` 生成新的 business.v2.so.目前业务插件仍可用第 3 步的手工命令编译.

`.gitignore` 已有 `*.so`、`*.so.*`,覆盖共享库产物,并加入 moduleTest、pluginSmoke 和 fixtures/ 的忽略规则.源码、共享接口和手写 Makefile 应提交.

## 9. 集成验收和提交边界

按顺序验证:

1. 独立插件加载程序输出 v1,错误场景返回非零.
2. 新配置默认禁用,旧配置照常使用;启用后能读取和解析插件路径.
3. 全量编译 server,并运行受影响的配置、业务、HTTP、SQL、启动失败回归.
4. 本地配置启用插件,启动后访问 GET `/api/version`.
5. 把插件正文改为 `TinyWebServer v2\n`,生成 `business.v2.so`;修改本地配置路径并重启后返回 v2.
6. 当前进程运行期间只编译 v2 或修改配置,接口仍返回已加载版本.本阶段这属于预期行为.

HTTP 访问示例:

```bash
curl http://localhost:8888/api/version
```

启用 HTTPS 时,沿用本机已经验证可用的证书与域名组合:

```bash
curl --cacert ./ssl/dev-root-ca.crt https://localhost:8888/api/version
```

后一个命令要求当前服务器证书由该 CA 签发且覆盖 localhost.要求客户端证书时,还需要沿用现有客户端证书参数.服务启动仍依赖有效数据库配置,独立加载程序则没有这个依赖.

建议把阶段六拆成两个可独立检查的提交:

- `feat(plugin): add C ABI and standalone shared library loader`:第 2～5 步及对应失败回归.
- `feat(server): load business plugin from configuration at startup`:配置、请求链、构建和文档集成.

只有运行中的同一个进程能从 v1 切换到 v2,且旧请求完成前不释放旧模块、新模块加载失败继续使用旧版本,才进入阶段七的热更新验收.启动加载完成后,再按总指南引入 PluginManager、请求级快照和重载触发入口.

## 10. 参考代码的验证范围

2026-10-28 从本文 5 个完整代码块提取到临时目录,独立编译并执行.插件和加载程序使用 C++23、-Wall、-Wextra、-Werror 编译通过;共享头另外通过 C11 语法检查,导出入口可被 nm 查到.

14 个运行检查通过:正常加载及释放后的正文使用、文件缺失、参数提示、错误 ABI、错误结构体大小、缺失必需函数、空函数表、create 失败、成功返回却没有实例、handle 失败、插件忽略正文超限错误、插件忽略无效正文参数错误、导出入口缺失、含 NUL 正文的按长度复制.失败场景通过临时变体插件制造.

2026-10-30 实际源码已实现 Module 和独立加载程序,新增 `make plugin-test` 执行 26 项回归及独立加载验证,具体范围见 [插件测试说明](../code/plugin/test/readme.md).

这些检查验证独立插件模块.服务器配置、请求链、完整网络和真实数据库的集成验收仍需在实现第 6～9 步后执行.
