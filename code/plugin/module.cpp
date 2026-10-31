#include "module.h"

#include <dlfcn.h>
#include <stdexcept>
#include <utility>

using namespace std;

namespace {

//当前插件只返回短文本,限制单次调用的累计正文为64KiB
constexpr size_t maxBodySize = 64 * 1024;

struct ResponseBuffer {
    string body;
    bool failed = false;
};

int Append(void* user, const char* data, size_t size) noexcept {
    if (user == nullptr) {
        return -1;
    }
    auto& buffer = *static_cast<ResponseBuffer*>(user);
    if (buffer.failed) {
        return -1;
    }
    if ((data == nullptr && size != 0) || size > maxBodySize - buffer.body.size()) {
        buffer.failed = true;
        return -1;
    }
    try {
        if (size != 0) {
            buffer.body.append(data, size);
        }
        return 0;
    } catch (...) {
        buffer.failed = true;
        return -1;
    }
}

} //namespace

shared_ptr<Module> Module::Load(const string& path) {
    if (path.empty() || path.find('\0') != string::npos) {
        throw invalid_argument("Plugin path must be nonempty and contain no NUL");
    }

    //先建立资源持有者,后续任一步失败都会经析构函数回收
    auto module = shared_ptr<Module>(new Module);
    module->handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module->handle_ == nullptr) {
        const char* error = dlerror();
        throw runtime_error("dlopen failed: " + string(error == nullptr ? "unknown error" : error));
    }

    dlerror();
    void* symbol = dlsym(module->handle_, "tinyweb_plugin_get_api");
    const char* error = dlerror();
    if (error != nullptr) {
        throw runtime_error("dlsym failed: " + string(error));
    }
    if (symbol == nullptr) {
        throw runtime_error("Plugin entry is null");
    }

    //Linux/POSIX允许把dlsym返回的地址转换为对应的函数指针
    using GetApi = const TinywebPluginApi* (*)(void);
    const auto getApi = reinterpret_cast<GetApi>(symbol);
    module->api_ = getApi();
    if (module->api_ == nullptr) {
        throw runtime_error("Plugin API is null");
    }
    if (module->api_->abiVersion != TINYWEB_PLUGIN_ABI_VERSION ||
        module->api_->structSize != sizeof(TinywebPluginApi)) {
        throw runtime_error("Plugin ABI version or structure size mismatch");
    }
    if (module->api_->create == nullptr || module->api_->handle == nullptr ||
        module->api_->destroy == nullptr) {
        throw runtime_error("Plugin API has missing functions");
    }

    const int result = module->api_->create(&module->instance_);
    if (result != 0 || module->instance_ == nullptr) {
        throw runtime_error("Plugin create failed");
    }
    return module;
}

Module::~Module() {
    //destroy和函数表都在库内,必须先销毁实例再关闭库句柄
    if (instance_ != nullptr) {
        api_->destroy(instance_);
    }
    if (handle_ != nullptr) {
        dlclose(handle_);
    }
}

string Module::Handle(const string& path) const {
    ResponseBuffer buffer;
    const TinywebRequestView request = {path.data(), path.size()};
    const TinywebResponseWriter writer = {&buffer, Append};
    const int result = api_->handle(instance_, &request, &writer);
    //记住回调失败,插件忽略append的错误码时也不接受这次结果
    if (result != 0 || buffer.failed) {
        throw runtime_error("Plugin handle failed");
    }
    return std::move(buffer.body);
}
