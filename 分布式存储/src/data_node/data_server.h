#pragma once

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/base/Timestamp.h>
#include "kv_service.h"
#include "raft_service.h"
#include <memory>
#include <cstdint>

class DataServer {
public:
    // Phase 1: 单节点（无 Raft）
    DataServer(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& addr,
               const std::string& name,
               const std::string& db_path);

    // Phase 2: Raft 集群模式
    DataServer(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& addr,
               uint64_t nodeId,
               const std::unordered_map<uint64_t, std::string>& peers,
               const std::string& db_path);

    void start();
    // 驱动 Raft 定时任务（选举超时检查、心跳发送、commit 推进）
    void tick();

private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp ts);
    void dispatchClientRpc(const muduo::net::TcpConnectionPtr& conn,
                            const dkv::kv::RpcMessage& msg);
    void dispatchRaftRpc(const muduo::net::TcpConnectionPtr& conn,
                          const dkv::raft::RaftMessage& msg);

    // 为 KvService 提供回调：写操作走 Raft
    void handleClientMessage(const muduo::net::TcpConnectionPtr& conn,
                              const dkv::kv::RpcMessage& msg);

    muduo::net::TcpServer server_;
    std::shared_ptr<RocksEngine> engine_;
    std::unique_ptr<KvService> service_;
    std::unique_ptr<RaftService> raft_;
    bool useRaft_ = false;
    uint64_t nodeId_ = 0;
};
