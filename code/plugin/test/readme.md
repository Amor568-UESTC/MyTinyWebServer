# 插件加载测试

最近同步:2026-10-30.

在本目录执行:

```bash
make
make test
make clean
```

项目根目录可以执行 `make plugin-test`.默认在本目录生成 moduleTest、pluginSmoke 和 fixtures/;也可以传入 BIN_DIR 指定独立输出目录.产物已加入 .gitignore.

## 验证范围

moduleTest 包含 26 项回归:

- 从真实 business.cpp 编译共享库,加载并取得正文,模块释放后正文仍可使用.
- 空路径、含 NUL 路径、文件缺失、入口缺失、空函数表、ABI 与大小不匹配、必需函数缺失.
- create 失败、成功却没有实例、错误地返回非空半成品时的回收.
- handle 失败、正文超限、无效正文参数,以及插件忽略回调失败时仍拒绝结果.
- 空正文、含 NUL 正文、多次 append、正好 64KiB 的累计正文.
- 最后一个共享引用释放前模块保持可用,实例销毁发生在库关闭前.
- 不支持的路径返回错误,4 个线程共 400 次调用使用独立正文缓冲区.

fixture.cpp 使用编译参数构建不同失败桩库,不替换主程序中的 dlopen/dlsym.桩库记录 C(create)、D(destroy)、U(库卸载回调),验证失败路径与释放顺序.桩库使用 `-fno-gnu-unique` 保持卸载可观察;这个测试不承诺任意业务库都在 dlclose 后立即解除映射.

fixtures/business.so 从仓库的 plugins/business.cpp 单独编译.测试不改写 plugins/business.v1.so,也不启动网络服务、不连接数据库.

## 独立加载程序

在项目根目录执行,可检查自己已经编译的业务插件:

```bash
./code/plugin/test/pluginSmoke ./plugins/business.v1.so
```

成功输出 TinyWebServer v1,失败时向 stderr 输出原因并返回 1.该程序先释放 Module,再输出主程序持有的正文.入口查找、资源回收和调用逻辑与未来服务器使用同一个 Module 实现.
