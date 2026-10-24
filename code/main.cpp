#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>

#include "server/webserver.h"
#include "config/configloader.h"

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "config/server.toml";
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: " << argv[0] << " [--config FILE]\n";
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--config") {
        configPath = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " [--config FILE]\n";
        return 1;
    }

    // 忽略 SIGPIPE 信号，避免写入失败导致服务器终止。
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, nullptr) == -1) {
        std::perror("sigaction(SIGPIPE)");
        return 1;
    }

    try {
        auto config = LoadConfig(configPath);
        WebServer server(std::move(config));
        server.Start();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
