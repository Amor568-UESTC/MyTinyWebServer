#pragma once

#include "handlerresult.h"
#include "userservice.h"
#include "../http/httprequest.h"

#include <memory>

class Module;

class RequestHandler {
public:
    using UserVerifier = bool (*)(const std::string&, const std::string&, bool);

    explicit RequestHandler(UserVerifier verifier = UserService::Verify,
                            std::shared_ptr<Module> module = nullptr);
    HandlerResult Handle(const HttpRequest& request) const;

private:
    UserVerifier verifier_;
    std::shared_ptr<Module> module_;
};
