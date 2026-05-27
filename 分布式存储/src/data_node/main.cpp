#include "data_server.h"
#include "config.h"
#include <signal.h>
#include <iostream>
#include <unordered_map>

using namespace muduo::net;

DataServer* g_server = nullptr;
EventLoop* g_loop = nullptr;

void onSigint(int) {
    std::cout << "\nShutting down..." << std::endl;
    if (g_loop) g_loop->quit();
}

int main(int argc, char* argv[]) {
    // 默认单节点模式
    std::string api_ip = "127.0.0.1";
    uint16_t api_port = 9100;
    std::string db_path = "/tmp/dkv_node1";

    // Raft 集群模式参数
    uint64_t nodeId = 0;
    std::unordered_map<uint64_t, std::string> peers; // nodeId -> "ip:raftPort"

    if (argc >= 4) {
        api_ip = argv[1];
        api_port = static_cast<uint16_t>(std::stoi(argv[2]));
        db_path = argv[3];
    }

    // Phase 2 集群模式: ./data_node <ip> <apiPort> <dbPath> <nodeId> <peer1Id>:<peer1RaftAddr> ...
    if (argc >= 5) {
        nodeId = std::stoull(argv[4]);
        for (int i = 5; i < argc; ++i) {
            std::string arg(argv[i]);
            size_t colon = arg.find(':');
            if (colon != std::string::npos) {
                uint64_t pid = std::stoull(arg.substr(0, colon));
                std::string addr = arg.substr(colon + 1);
                peers[pid] = addr;
            }
        }
        // 把自己也加入 peers 列表（getLeaderAddr 需要查自己的地址）
        if (peers.find(nodeId) == peers.end()) {
            peers[nodeId] = std::string(argv[1]) + ":" + std::to_string(api_port);
        }
    }

    signal(SIGINT, onSigint);
    signal(SIGTERM, onSigint);

    EventLoop loop;
    g_loop = &loop;

    InetAddress addr(api_ip.c_str(), api_port);

    DataServer* server = nullptr;
    if (nodeId > 0 && !peers.empty()) {
        std::cout << "Starting in Raft cluster mode, nodeId=" << nodeId
                  << ", peers=" << peers.size() << std::endl;
        server = new DataServer(&loop, addr, nodeId, peers, db_path);
    } else {
        std::cout << "Starting in standalone mode" << std::endl;
        server = new DataServer(&loop, addr, "dkv-standalone", db_path);
    }

    g_server = server;
    server->start();

    // Raft tick 定时器（每 10ms 驱动选举超时检查和心跳发送）
    if (nodeId > 0) {
        loop.runEvery(0.01, [server]() { server->tick(); });
    }

    loop.loop();
    delete server;
    return 0;
}
