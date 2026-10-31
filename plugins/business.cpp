#include "plugin_api.h"

#include <string>
#include <string_view>

using namespace std;

namespace {

//最小演示业务,后续复杂逻辑交给独立的Handler或Service
struct BusinessState {
    const string version = "TinyWebServer v1\n";
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

        //路径不要求以NUL结尾,按主程序给定的字节数匹配
        const string_view path(request->path, request->pathSize);
        if (path != "/api/version") {
            return -1;
        }

        const auto& state = *static_cast<const BusinessState*>(instance);
        //append在返回前复制正文,插件不把内部字符串的所有权交出去
        return writer->append(writer->user, state.version.data(), state.version.size());
    } catch (...) {
        return -1;
    }
}

void Destroy(void* instance) noexcept {
    delete static_cast<BusinessState*>(instance);
}

} //namespace

//函数表在模块加载期间保持有效,只导出这个固定入口
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
