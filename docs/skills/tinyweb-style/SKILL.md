---
name: tinyweb-style
description: 在MyTinyWebServer仓库新增、修改或审查C++代码、TOML配置、Makefile及测试时,遵循作者约定的命名、注释、排版和模块目录风格.
---

# TinyWeb 仓库风格参考

用于本仓库的新增代码、局部修改和代码审查.以下路径均相对于项目根目录.

## 使用原则

- 项目说明和代码注释主要使用中文,标识符与配置键沿用现有英文命名.
- 用户明确提出的习惯优先于旧代码中的历史写法.仓库存在混合风格,不能据此恢复省略大括号等旧写法.
- 修改时对齐所在模块,不要为了风格统一而重排整个文件、重命名现有接口或批量格式化无关代码.

## 目录与文件组织

- 代码按职责放在 `code/config`、`buffer`、`http`、`handler`、`log`、`pool`、`server`、`timer` 等模块中,入口为 `code/main.cpp`.
- 类声明通常放在 `.h`,实现放在对应 `.cpp`;模板及必要的内联实现可以保留在 `.h` 或 `.hpp`.
- 文件名跟随模块已有习惯,如 `sqlconnpool.cpp`、`configloader.h`;保留 `sqlconnRAII.h`、`sslContext.cpp` 等现有大小写.
- 模块说明沿用 `code/模块/readme.md`,测试说明放在对应 `test/readme.md`,实施计划和已知问题放在 `docs/`.

## C++ 排版与命名空间

- 新代码使用4个空格缩进.函数、类的大括号是否另起一行,沿用当前文件附近的写法.
- `if/else/for/while/do` 的控制体始终写完整 `{}`,单条语句也不省略.
- 代码保留必要空格,例如 `if (condition)`、`a = b + c`、`Func(a, b)`.注释少空格不代表代码也全部挤在一起.
- 指针和引用通常写成 `MYSQL* sql`、`const string& value`,沿用当前模块的声明习惯.
- `.cpp` 通常在所有 `#include` 之后写 `using namespace std;`,沿用作者习惯.需要避免歧义或明确来源时可以保留 `std::`.
- 头文件使用 `std::string`、`std::mutex` 等限定名,不在全局写 `using namespace std;`.
- `.cpp` 优先包含自身对应的头文件,其他头文件按所在文件现有分组排列.新增头文件通常使用 `#pragma once`.
- 项目当前按C++23构建.旧测试已有独立编译约定时,保留其约定.

## 注释

- 主要使用简短中文注释,`//` 后不自动加空格.
- 注释使用半角标点,例如 `, . : ; ()`,减少不必要的空格.技术名称保持原有拼写.
- 示例: `//初始化连接池,失败时抛出异常`.
- 解释操作意图、资源归属、有效期或失败处理,避免逐行复述代码.
- 不因个人习惯统一给旧注释补空格,也不自动替换成全角标点.

## 命名

- 类、结构体通常用大驼峰,如 `WebServer`、`SqlConnPool`、`ServerConfig`.
- 局部变量、参数、公开配置字段用小驼峰,如 `connSize`、`passwordEnv`、`idleTimeoutMs`.
- 普通成员变量通常带尾部 `_`,如 `connQue_`、`closed_`;配置结构体的公开字段不添加尾部 `_`.
- 函数名沿用所在类的惯例,如 `Init`、`GetConn`;私有辅助函数常带尾部 `_`,如 `InitThread_`、`initSocket_`.
- 保留现有 `read/write/parse`、枚举值和宏等接口的原有命名,不为统一大小写而改动调用链.

## 配置与文档

- TOML字段使用小驼峰且区分大小写,与C++配置字段保持一致,如 `workerThreads`、`passwordEnv`.
- 新增配置字段时同步读取逻辑、字段白名单、示例和相关说明.
- 公共模板放在 `config/server.example.toml`;`config/server.toml`、`.env` 和 `ssl/` 是本机文件,示例中只放占位值.

## 测试与构建组织

- 新测试放在 `code/模块/test/`,通常命名为 `xxxTest.cpp`,与旧测试的小目录组织方式对齐.
- 各 `test/` 目录自带 Makefile 和 readme,默认在本目录生成测试程序,并将新增产物加入 `.gitignore`.
- 目录内统一用 `make` 编译、`make test` 编译并运行、`make clean` 清理该Makefile的产物.
- 根目录和 `build/Makefile` 的测试目标只转发,具体测试编译规则留在模块的 `test/Makefile`.
- 保留旧测试的源码、路径和已有独立编译方式,迁移或清理须属于当前任务范围.
- Makefile命令行使用Tab缩进,递归构建使用 `$(MAKE) -C 目录`.
- 服务器构建只将 `.cpp` 作为独立输入,头文件通过 `#include` 引入.保留先构建临时产物、成功后重命名替换服务器程序的流程.

## 局部格式示例

```cpp
#include "sqlconnpool.h"

using namespace std;

MYSQL* SqlConnPool::GetConn() {
    //检查与取出放在同一个临界区
    lock_guard<mutex> locker(mtx_);
    if (!initialized_ || closed_ || connQue_.empty()) {
        return nullptr;
    }
    MYSQL* sql = connQue_.front();
    connQue_.pop();
    return sql;
}
```

## 修改后的验证

- 按受影响模块选择现有入口: `make config-test`、`make sql-test`、`make http-test`、`make log-test`、`make server-test`、`make handler-test`,或进入模块的 `test/` 执行 `make test`.
- 服务器源码或构建逻辑变化时运行相应编译检查;纯注释和格式调整无需新增测试.
- 提交前检查 `git diff --check`,确认修改范围以及测试产物的忽略规则.
