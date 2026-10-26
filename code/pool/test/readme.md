# 池模块测试

测试源码、构建规则和说明放在本目录.默认在当前目录生成测试程序,与旧测试的使用方式一致.

## SQL 连接池回归

以下命令在本目录执行:

```bash
make
make test
```

`make` 只编译 `sqlconnpoolTest`,`make test` 编译并运行全部 15 个场景.也可以从项目根目录执行 `make sql-test`,该命令会转发到这里.

查看场景或单独运行:

```bash
./sqlconnpoolTest --list
./sqlconnpoolTest concurrent
```

测试通过链接器包装模拟 MySQL API,无需真实 MySQL 服务、数据库账号或证书.每个场景在独立进程中运行,失败返回非零退出码,Makefile 为每个场景设置 10 秒超时.

覆盖初始化失败与资源回滚、借还和池耗尽、RAII 存活期与提前返回、关闭后归还、重复初始化、并发独占、线程状态,以及无连接时的登录失败路径.这些检查不替代真实账号认证、SQL 查询、完整登录注册或正常停机的验收.

默认产物 `code/pool/test/sqlconnpoolTest` 已加入 `.gitignore`,可通过 `BIN_DIR` 指定其他输出目录.清理它:

```bash
make clean
```

## ThreadPoolV2 原有测试

[threadPoolV2Test.cpp](threadPoolV2Test.cpp) 保留原来的单独编译方式,不由本目录 Makefile 默认构建.该测试包含基础任务、优先级、返回值、异常、并发和性能场景.

在本目录编译运行:

```bash
g++ -std=c++17 -pthread -g -O0 -Wall -Wextra threadPoolV2Test.cpp -o testThreadPool
./testThreadPool
```

原有 `testThreadPool` 二进制保留,本目录的 `make clean` 只清理新 Makefile 生成的 SQL 连接池测试程序.
