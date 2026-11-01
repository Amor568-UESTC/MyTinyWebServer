#pragma once

#include <string>

class UserService {
public:
    static bool Verify(const std::string& name, const std::string& password, bool isLogin);
};
