# 业务处理层

本模块把路由和数据库业务从 HTTP 解析器中分离.主程序负责路由匹配,启用插件后将 GET /api/version 交给启动时加载的 .so.运行期间版本切换属于后续阶段.

| 文件 | 职责 |
| --- | --- |
| `handlerresult.h` | 业务结果,描述状态码、文件路径或自有文本正文及类型 |
| `requesthandler.h/.cpp` | 页面别名、登录注册分派与 GET /api/version |
| `userservice.h/.cpp` | 通过 SQL 连接池执行登录注册查询 |

调用流程为 `HttpRequest → RequestHandler → HandlerResult → HttpResponse`.登录注册由 RequestHandler 调用 UserService.业务层不操作 socket、epoll、SSL、iovec 或文件映射,也不改写解析器保存的原始路径.

## 当前路由

- `/` 映射到 `/index.html`.
- `/index`、`/login`、`/register`、`/welcome`、`/video`、`/picture` 保留原有 .html 别名.
- POST 表单提交到 `/login`、`/login.html`、`/register`、`/register.html` 时进行验证,成功选择 `/welcome.html`,失败选择 `/error.html`,沿用原页面响应方式.
- GET `/api/version` 在禁用插件时返回内置 `TinyWebServer v1\n`,启用时返回插件正文,类型为 `text/plain; charset=utf-8`.插件失败返回 500 和通用错误正文,不回退成成功响应.
- 其他请求交给原文件响应路径.当前按完整路径精确匹配,尚未分离查询参数.

`/api/version` 的业务逻辑不访问数据库,但服务器启动仍会初始化数据库连接池,正常启动需要有效的数据库配置.

## 数据与资源归属

HandlerResult 的字符串由结果对象持有.文本响应复制进主程序缓冲区后才结束本次处理;文件响应仍由 HttpResponse 通过资源目录约束打开并映射.原始请求路径不会因路由或登录结果改变.

HandlerResult 是主程序内部的 C++ 类型,后续插件不能直接把它作为跨 .so 的稳定接口.第一版插件只迁移 `/api/version`,使用独立 C ABI 和由主程序提供的正文复制回调;数据库业务继续留在主程序.

## 登录注册

UserService 使用具名 SqlConnRAII 借还连接,使用预处理语句绑定输入参数,不拼接用户名或密码到 SQL.登录比较采用二进制转换保持密码大小写敏感,注册查询失败或插入失败均返回 false.语句由 RAII 关闭,日志不记录密码和绑定参数.

本轮保持现有 user 表和页面流程.密码仍按原有明文方式存储,也没有增加会话或权限管理.注册的并发唯一性仍需要数据库唯一约束等后续业务设计,不能把这次拆分视为完整认证系统.

RequestHandler 的构造参数允许替换验证函数,便于不依赖 MySQL 测试路由.线上默认使用 UserService::Verify.第二个参数接收 shared_ptr<Module>,由 HttpConn 复制连接持有的模块引用,保证处理期间插件仍有效.当前匹配方法和完整路径,尚未实现动态路由注册或查询参数拆分.

## 回归

项目根目录执行 `make handler-test`,或进入 [test/](test/readme.md) 执行 `make test`.
