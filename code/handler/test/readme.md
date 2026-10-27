# 业务层测试

本目录执行 `make test`,或在项目根目录执行 `make handler-test`.单独 make 只编译,默认生成 `requesthandlerTest` 和 `userserviceTest`,`make clean` 清理这两个程序.可以通过 BIN_DIR 指定其他输出目录.

requesthandlerTest 包含 22 项路由检查,用替代验证函数检查页面别名、登录注册参数和结果、原始请求不被改写、非业务请求不触发验证,以及版本接口返回自有文本.该测试不链接 MySQL 客户端库.

userserviceTest 包含 14 项模拟数据库检查,覆盖登录成功或失败、注册与重复用户、查询与插入失败、预处理各阶段错误、资源回收,以及引号和 NUL 字节作为参数传递.每个场景独立运行并有超时限制,无需真实数据库.

这些测试不验证真实数据库的字符集、排序规则、权限、账号认证或并发注册唯一性.连接池的其他行为仍由 `make sql-test` 验证,HTTP 解析到文件或文本响应的处理链由 `make http-test` 验证.
