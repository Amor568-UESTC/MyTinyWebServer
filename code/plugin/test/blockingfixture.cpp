#include "../plugin_api.h"

#include <cstdio>
#include <cstdlib>
#include <new>

using namespace std;

namespace {
void (*hook)(void*) = nullptr;
void* context = nullptr;

void Trace(char event) noexcept {
    const char* path = getenv("TINYWEB_PLUGIN_TEST_TRACE");
    if (path != nullptr) {
        FILE* file = fopen(path, "ab");
        if (file != nullptr) {
            fputc(event, file);
            fclose(file);
        }
    }
}

int Create(void** instance) noexcept {
    Trace('C');
#ifdef BLOCK_CREATE
    if (hook != nullptr) {
        hook(context);
    }
#endif
    *instance = new (nothrow) int(1);
    return *instance == nullptr ? -1 : 0;
}

int Handle(void*, const TinywebRequestView*, const TinywebResponseWriter* writer) noexcept {
#ifndef BLOCK_CREATE
    if (hook != nullptr) {
        hook(context);
    }
#endif
    return writer->append(writer->user, "TinyWebServer v1\n", 17);
}

void Destroy(void* instance) noexcept {
    Trace('D');
    delete static_cast<int*>(instance);
}

__attribute__((destructor)) void OnUnload() {
    Trace('U');
}
} //namespace

//仅供测试安装阻塞点,设置发生在启动测试线程之前
extern "C" __attribute__((visibility("default")))
void fixture_set_hook(void (*callback)(void*), void* user) {
    hook = callback;
    context = user;
}

extern "C" __attribute__((visibility("default")))
const TinywebPluginApi* tinyweb_plugin_get_api(void) {
    static const TinywebPluginApi api = {
        TINYWEB_PLUGIN_ABI_VERSION, sizeof(TinywebPluginApi), Create, Handle, Destroy
    };
    return &api;
}
