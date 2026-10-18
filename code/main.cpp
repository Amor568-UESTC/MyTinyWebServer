#include<unistd.h>
#include <signal.h>
#include <cstdio>

#include"server/webserver.h"

int main()
{
    //忽略SIG_PIPE信号,避免写入失败导致服务器终止
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, nullptr) == -1) {
        std::perror("sigaction(SIGPIPE)");
        return 1;
    }

    // TODO: 如需启用 HTTPS，请先生成证书文件，然后设置 enableHttps=true 并填入证书路径
    WebServer server(
        8888,3,60000,false,
        3306,"root","123","yourdb",
        12,6,true,1,1024,
        true,"./ssl/lan-server.crt","./ssl/lan-server.key",nullptr
    );
    server.Start();
}