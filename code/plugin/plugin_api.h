#pragma once

#include <stddef.h>
#include <stdint.h>

//接口布局或调用约定变化时升级,与业务版本号独立
#define TINYWEB_PLUGIN_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

//请求数据由主程序持有,仅在本次handle调用期间有效
typedef struct {
    const char* path;
    //路径字节数,不包含结尾NUL,插件按长度读取
    size_t pathSize;
} TinywebRequestView;

//主程序提供正文写入回调,插件不保存writer或user供以后使用
typedef struct {
    void* user;
    //返回前复制正文,0表示成功,非0表示失败,异常不越过接口
    int (*append)(void* user, const char* data, size_t size);
} TinywebResponseWriter;

//主程序与插件使用兼容的目标平台和ABI,接口函数不向外抛出异常
typedef struct {
    uint32_t abiVersion;
    //第一版要求与sizeof(TinywebPluginApi)精确相等
    uint32_t structSize;

    //0表示成功并输出非空实例,失败时自行清理并输出空指针
    int (*create)(void** instance);

    //0表示成功,非0表示失败,并发调用时实例只读或由插件同步
    int (*handle)(void* instance, const TinywebRequestView* request,
                  const TinywebResponseWriter* writer);

    //由创建实例的插件销毁,必须在关闭库句柄之前调用
    void (*destroy)(void* instance);
} TinywebPluginApi;

//固定导出入口,返回的函数表在模块加载期间有效
__attribute__((visibility("default")))
const TinywebPluginApi* tinyweb_plugin_get_api(void);

#ifdef __cplusplus
}
#endif
