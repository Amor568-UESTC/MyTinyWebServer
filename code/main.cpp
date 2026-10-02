#include<unistd.h>
#include"server/webserver.h"

int main()
{
    // TODO: 如需启用 HTTPS，请先生成证书文件，然后设置 enableHttps=true 并填入证书路径
    WebServer server(
        8888,3,60000,false,
        3306,"root","568","yourdb",
        12,6,true,1,1024,
        true,"./ssl/lan-server.crt","./ssl/lan-server.key",nullptr
    );
    server.Start();
}