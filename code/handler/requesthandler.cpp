#include "requesthandler.h"

#include "../log/log.h"
#include "../plugin/module.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

using namespace std;

RequestHandler::RequestHandler(UserVerifier verifier, shared_ptr<Module> module)
    : verifier_(verifier), module_(std::move(module)) {
    if (verifier_ == nullptr) {
        throw invalid_argument("User verifier must not be null");
    }
}

HandlerResult RequestHandler::Handle(const HttpRequest& request) const {
    HandlerResult result;
    result.path = request.path();
    if (request.method() == "GET" && result.path == "/api/version") {
        result.kind = HandlerResult::Kind::Text;
        result.path.clear();
        if (module_ == nullptr) {
            result.body = "TinyWebServer v1\n";
            return result;
        }
        try {
            result.body = module_->Handle(request.path());
        } catch (const exception& error) {
            LOG_ERROR("Plugin request failed: %s", error.what());
            result.status = 500;
            result.body = "Internal Server Error\n";
        }
        return result;
    }

    //页面别名属于路由规则,不修改解析器保存的请求路径
    static const unordered_set<string> pages = {
        "/index", "/register", "/login", "/welcome", "/video", "/picture"
    };
    if (result.path == "/") {
        result.path = "/index.html";
    } else if (pages.contains(result.path)) {
        result.path += ".html";
    }

    if (request.method() == "POST" && request.IsFormUrlencoded() &&
        (result.path == "/login.html" || result.path == "/register.html")) {
        const bool isLogin = result.path == "/login.html";
        const bool accepted = verifier_(request.GetPost("username"), request.GetPost("password"), isLogin);
        result.path = accepted ? "/welcome.html" : "/error.html";
    }
    return result;
}
