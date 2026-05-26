#include"chatserver.h"
#include<signal.h>
#include"mysql.h"
#include"ConnectPool.h"
void handlerfun(int sig)
{
    MySQL mysql;
    if(mysql.connect())
    {
        mysql.update("UPDATE user SET state='offline' WHERE state='online';");
        exit(0);
    }
}

int main(int argc, char* argv[])
{
    // 信号处理：Ctrl+C 关闭服务器，修改用户在线状态
    signal(SIGINT, handlerfun);

    // ===================== 动态指定 IP 和 端口 =====================
    std::string ip = "127.0.0.1";  // 默认IP
    uint16_t port = 8080;          // 默认端口

    // 如果运行时传入了参数：./server 192.168.1.100 9999
    if (argc == 3) {
        ip = argv[1];
        port = atoi(argv[2]);
    }

    std::cout << "服务器启动地址：" << ip << ":" << port << std::endl;
    // ==============================================================

    EventLoop loop;
    InetAddress ia(ip.c_str(), port);
    ChatServer c(&loop, ia, "server01");

    c.start();
    loop.loop();
    return 0;
    
    
    
}