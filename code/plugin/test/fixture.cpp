#include "../plugin_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace std;

namespace {

enum class FixtureMode {
    valid, nullApi, badAbi, badSize, missingCreate, missingHandle, missingDestroy,
    createFailure, nullInstance, partialCreateFailure, handleFailure,
    oversizedBody, ignoredAppendFailure, invalidBody, ignoredInvalidBody,
    emptyBody, binaryBody, exactLimit, chunkedBody, versionTwo
};

#ifndef FIXTURE_MODE
#define FIXTURE_MODE FixtureMode::valid
#endif

constexpr FixtureMode mode = FIXTURE_MODE;

void Trace(char event) noexcept {
    const char* path = getenv("TINYWEB_PLUGIN_TEST_TRACE");
    if (path == nullptr) {
        return;
    }
    FILE* file = fopen(path, "ab");
    if (file != nullptr) {
        fputc(event, file);
        fclose(file);
    }
}

//桩库只使用普通整数状态,便于观察最后一次dlclose触发的清理顺序
__attribute__((destructor)) void OnUnload() {
    Trace('U');
}

[[maybe_unused]] int Create(void** instance) noexcept {
    *instance = nullptr;
    Trace('C');
    if (mode == FixtureMode::createFailure) {
        return -1;
    }
    if (mode == FixtureMode::nullInstance) {
        return 0;
    }
    *instance = new (nothrow) int(1);
    if (*instance == nullptr) {
        return -1;
    }
    if (mode == FixtureMode::partialCreateFailure) {
        return -1;
    }
    return 0;
}

[[maybe_unused]] int Handle(void*, const TinywebRequestView*,
                           const TinywebResponseWriter* writer) noexcept {
    if (mode == FixtureMode::handleFailure) {
        return -1;
    }
    if (mode == FixtureMode::oversizedBody || mode == FixtureMode::ignoredAppendFailure) {
        const int result = writer->append(writer->user, "x", 64 * 1024 + 1);
        return mode == FixtureMode::ignoredAppendFailure ? 0 : result;
    }
    if (mode == FixtureMode::invalidBody || mode == FixtureMode::ignoredInvalidBody) {
        const int result = writer->append(writer->user, nullptr, 1);
        return mode == FixtureMode::ignoredInvalidBody ? 0 : result;
    }
    if (mode == FixtureMode::emptyBody) {
        return writer->append(writer->user, nullptr, 0);
    }
    if (mode == FixtureMode::binaryBody) {
        return writer->append(writer->user, "A\0B", 3);
    }
    if (mode == FixtureMode::versionTwo) {
        return writer->append(writer->user, "TinyWebServer v2\n", 17);
    }
    if (mode == FixtureMode::exactLimit) {
        char chunk[1024];
        memset(chunk, 'x', sizeof(chunk));
        for (int i = 0; i < 64; ++i) {
            const int result = writer->append(writer->user, chunk, sizeof(chunk));
            if (result != 0) {
                return result;
            }
        }
        return 0;
    }
    if (mode == FixtureMode::chunkedBody) {
        const int result = writer->append(writer->user, "Tiny", 4);
        if (result != 0) {
            return result;
        }
        return writer->append(writer->user, "WebServer v1\n", 13);
    }
    return writer->append(writer->user, "TinyWebServer v1\n", 17);
}

[[maybe_unused]] void Destroy(void* instance) noexcept {
    Trace('D');
    delete static_cast<int*>(instance);
}

} //namespace

#ifdef FIXTURE_NO_ENTRY
extern "C" __attribute__((visibility("default")))
int other_entry(void) {
    return 0;
}
#else
extern "C" __attribute__((visibility("default")))
const TinywebPluginApi* tinyweb_plugin_get_api(void) {
    if (mode == FixtureMode::nullApi) {
        return nullptr;
    }
    static const TinywebPluginApi api = {
        TINYWEB_PLUGIN_ABI_VERSION + (mode == FixtureMode::badAbi ? 1u : 0u),
        static_cast<uint32_t>(sizeof(TinywebPluginApi)) - (mode == FixtureMode::badSize ? 1u : 0u),
        mode == FixtureMode::missingCreate ? nullptr : Create,
        mode == FixtureMode::missingHandle ? nullptr : Handle,
        mode == FixtureMode::missingDestroy ? nullptr : Destroy
    };
    return &api;
}
#endif
