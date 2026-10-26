#pragma once

#include <filesystem>

#include "serverconfig.h"

// 读取并校验 TOML，文件内的相对路径以配置文件目录为基准。
// database.password 与 passwordEnv 互斥；后者读取已设置且非空的进程环境变量。
// 不自动读取 .env 文件。
// 失败时抛出 std::runtime_error。
ServerConfig LoadConfig(const std::filesystem::path& path);
