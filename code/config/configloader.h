#pragma once

#include <filesystem>

#include "serverconfig.h"

// 读取并校验 TOML，文件内的相对路径以配置文件目录为基准。
// 失败时抛出 std::runtime_error。
ServerConfig LoadConfig(const std::filesystem::path& path);
