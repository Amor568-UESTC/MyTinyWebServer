#pragma once

#include <string>

//业务结果只描述响应内容,连接和文件资源仍由主程序管理
struct HandlerResult {
    enum class Kind { File, Text };

    Kind kind = Kind::File;
    int status = 200;
    std::string path;
    std::string body;
    std::string contentType = "text/plain; charset=utf-8";
};
