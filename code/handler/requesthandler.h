#pragma once

#include "handlerresult.h"
#include "userservice.h"
#include "../http/httprequest.h"

class RequestHandler {
public:
    using UserVerifier = bool (*)(const std::string&, const std::string&, bool);

    explicit RequestHandler(UserVerifier verifier = UserService::Verify);
    HandlerResult Handle(const HttpRequest& request) const;

private:
    UserVerifier verifier_;
};
