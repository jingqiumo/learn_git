#include "pd_server.h"
#include <signal.h>
#include <iostream>

using namespace muduo::net;

EventLoop* g_loop = nullptr;

void onSigint(int) {
    std::cout << "\n[PD] Shutting down..." << std::endl;
    if (g_loop) g_loop->quit();
}

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 8100;
    int numShards = 1;
    int replicaCount = 3;

    if (argc >= 3) {
        ip = argv[1];
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc >= 4) numShards = std::stoi(argv[3]);

    signal(SIGINT, onSigint);
    signal(SIGTERM, onSigint);

    std::cout << "[PD] Starting on " << ip << ":" << port
              << ", shards=" << numShards << std::endl;

    EventLoop loop;
    g_loop = &loop;
    InetAddress addr(ip.c_str(), port);
    PdServer server(&loop, addr, numShards, replicaCount);

    server.start();

    // 每 5 秒执行调度
    loop.runEvery(5.0, [&server]() { server.tick(); });

    loop.loop();
    return 0;
}
