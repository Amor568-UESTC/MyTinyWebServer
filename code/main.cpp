#include<unistd.h>
#include"server/webserver.h"

int main()
{
    WebServer server(
        8888,3,60000,false,
        3306,"root","568","webserver",
        12,6,true,1,1024
    );
    server.Start();
}