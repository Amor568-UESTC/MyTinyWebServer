# 插件加载模块

最近同步:2026-10-30.

本模块已实现独立业务插件的加载和调用,尚未接入 WebServer、配置和 HTTP 请求分派.运行期间版本切换仍待后续实现.

| 文件 | 职责 |
| --- | --- |
| `plugin_api.h` | 主程序与插件共同使用的 C 接口 |
| `module.h/.cpp` | 库句柄、函数表和插件实例的 RAII 管理 |
| `../../plugins/business.cpp` | 当前返回版本文本的插件实现,单独编译 |
| `test/` | 独立加载程序、真实共享库和失败桩库回归 |

## 加载与调用

```cpp
auto module = Module::Load("./plugins/business.v1.so");
std::string body = module->Handle("/api/version");
```

Load 按顺序完成 dlopen、dlsym、ABI 与函数表校验、create.返回后模块才可使用;加载失败抛异常,已取得的资源通过析构回收.空路径和含 NUL 的库路径直接拒绝.

RTLD_NOW 在加载时解析依赖符号,RTLD_LOCAL 不主动向随后加载的库发布插件符号.路径中的 `/` 表示按路径查找,独立调用时建议使用 `./plugins/...` 或绝对路径.详见 [dlopen 手册](https://man7.org/linux/man-pages/man3/dlopen.3.html).

dlsym 前先清除旧 dlerror,之后再次读取 dlerror 判断失败,入口地址还额外要求非空.详见 [dlsym 手册](https://man7.org/linux/man-pages/man3/dlsym.3.html).

## 所有权与错误

- Module 同时持有库句柄、函数表和实例.最后一个共享引用释放时,先 destroy 实例,再 dlclose 库句柄.
- 不复制或移动 Module 本身,需要共享时复制 shared_ptr.调用 Handle 的整个过程应保留共享引用.
- Handle 使用本次调用独立的正文缓冲区,append 返回前复制数据.返回 string 由主程序持有,发送正文不需要继续引用插件内部字符串.
- 当前累计正文上限为 64KiB,空正文和含 NUL 的正文按字节长度处理.文件响应不经过这里,不受此限制.
- handle 失败或任一次 append 失败都抛异常,不返回部分正文.插件忽略 append 错误时,缓冲区中的 failed 标志仍会令调用失败.
- shared_ptr 保护模块有效期,可变实例状态仍由插件同步.目前 BusinessState 创建后只读.

dlclose 释放一次加载引用,不保证所有共享库立即从地址空间移除.当前关闭线程、连接和 SSL 的协调问题仍按原计划单独处理.

## extern "C" 与导出属性

`extern "C"` 指定 C 语言链接约定.在当前 Linux/GCC 环境下,入口使用固定名字 `tinyweb_plugin_get_api`,便于 dlsym 查找,避免 C++ 名字编码带来的变化.函数体仍可使用 C++ 的 string、new 等能力.它不自动统一 C++ 类型布局或内存归属.语言链接规则见 [C++ 工作草案](https://eel.is/c++draft/dcl.link).

`__attribute__((visibility("default")))` 是 GCC 的符号可见性属性.在使用 `-fvisibility=hidden` 构建时,它显式保留入口的外部可见性.详见 [GCC 代码生成选项](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html).

两者分别控制入口的链接方式和可见性.Create、Handle、Destroy 位于匿名命名空间,不单独导出名字;主程序通过函数表中的地址调用它们.

## 编译选项

在项目根目录首次构建一个尚未加载的版本:

```bash
g++ -std=c++23 -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden \
    -Icode/plugin plugins/business.cpp -o plugins/business.v1.so
```

| 选项 | 含义 |
| --- | --- |
| `g++` | 使用 GCC 的 C++ 编译驱动,链接 C++ 标准库 |
| `-std=c++23` | 选择 C++23 语言标准 |
| `-O2` | 启用二级优化 |
| `-Wall` | 开启一组常见警告,不代表全部警告 |
| `-Wextra` | 开启另一组额外警告 |
| `-fPIC` | 生成适用于共享库的位置无关代码 |
| `-shared` | 链接为共享库 |
| `-fvisibility=hidden` | 默认隐藏符号,按声明显式导出入口 |
| `-Icode/plugin` | 将该目录加入头文件查找路径 |
| `-o plugins/business.v1.so` | 指定输出文件 |

对应文档:[语言标准](https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html)、[优化](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)、[警告](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)、[代码生成](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)、[链接](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html)、[头文件目录](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html).

加载程序额外链接 `-ldl`,使用 dlopen/dlsym/dlclose.测试中的 `-pthread` 用于并发调用验证,`-g` 保留调试信息.后续修改插件时生成新的版本文件,不直接编译覆盖仍被加载的 `.so`.

## 验证

根目录执行 `make plugin-test`,或进入 [test/](test/readme.md) 执行 `make test`.当前包含 26 项模块回归,并运行独立加载程序.现有服务器请求仍走静态业务层,服务器接入步骤见 [实践指南](../../docs/plugin-startup-guide.md).
