#pragma once

#include "shard_manager.h"
#include "pd_service.h"
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <memory>

class PdServer {
public:
    PdServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& addr,
             int numShards, int replicaCount);

    void start();

    // 定时调度
    void tick();

private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp ts);

    muduo::net::TcpServer server_;
    ShardManager shardMgr_;
    PdService service_;
};
