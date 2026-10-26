# 配置读取测试

测试源码和独立 Makefile 位于本目录,与其他模块的 test 目录保持一致.

在本目录执行:

```bash
make
./configloaderTest
```

也可以直接编译并运行:

```bash
make test
```

项目根目录的 `make config-test` 会转发到本目录.默认程序为 `code/config/test/configloaderTest`,已加入 `.gitignore`.可以用 `BIN_DIR` 指定其他输出目录.

依赖支持 C++23 的 g++、make、pkg-config 和系统 toml++.MySQL 服务、真实证书以及本机数据库密码均不是测试前提.

当前 59 项检查覆盖 TOML 字段、默认值、类型和范围、未知字段、路径处理、环境变量密码、来源冲突和错误诊断.测试自动创建并清理临时资源,失败返回非零退出码.

清理本目录生成的测试程序:

```bash
make clean
```
