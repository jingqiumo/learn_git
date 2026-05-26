#include "pd_server.h"
#include "pd_rpc.pb.h"
#include <muduo/base/Timestamp.h>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

using namespace muduo::net;
using muduo::Timestamp;

PdServer::PdServer(EventLoop* loop,
                   const InetAddress& addr,
                   int numShards, int replicaCount)
    : server_(loop, addr, "dkv-pd")
    , service_(&shardMgr_)
{
    server_.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) { onConnection(conn); });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp ts) {
            onMessage(conn, buf, ts);
        });

    shardMgr_.initShards(numShards, replicaCount);
}

void PdServer::start() {
    server_.start();
    std::cout << "[PD] Server started on " << server_.ipPort() << std::endl;
}

void PdServer::tick() {
    auto dead = shardMgr_.getDeadNodes();
    if (!dead.empty()) {
        shardMgr_.scheduleRebalance();
    }
}

void PdServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        std::cout << "[PD] Node connected: " << conn->peerAddress().toIpPort()
                  << std::endl;
    } else {
        std::cout << "[PD] Node disconnected: " << conn->peerAddress().toIpPort()
                  << std::endl;
    }
}

void PdServer::onMessage(const TcpConnectionPtr& conn,
                          Buffer* buf, Timestamp ts) {
    (void)ts;
    while (buf->readableBytes() >= 4) {
        uint32_t len = 0;
        memcpy(&len, buf->peek(), 4);
        len = ntohl(len);
        if (buf->readableBytes() < 4 + len) break;

        buf->retrieve(4);
        std::string payload = buf->retrieveAsString(len);

        // 尝试解析 RegisterNodeRequest
        dkv::pd::RegisterNodeRequest regReq;
        if (regReq.ParseFromString(payload)) {
            service_.handleRegisterNode(conn, regReq);
            continue;
        }

        // 尝试解析 HeartbeatRequest
        dkv::pd::HeartbeatRequest hbReq;
        if (hbReq.ParseFromString(payload)) {
            service_.handleHeartbeat(conn, hbReq);
            continue;
        }

        // 尝试解析 GetShardMapRequest
        dkv::pd::GetShardMapRequest gsmReq;
        if (gsmReq.ParseFromString(payload)) {
            service_.handleGetShardMap(conn, gsmReq);
            continue;
        }
    }
}
