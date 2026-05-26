#pragma once

#include "raft_node.h"
#include "rocks_engine.h"
#include "kv_rpc.pb.h"
#include "raft_rpc.pb.h"
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Timestamp.h>
#include <memory>
#include <unordered_map>
#include <cstdint>

// RaftService 集成 Raft 共识、RocksDB 存储 和 节点间 RPC 通信
class RaftService {
public:
    RaftService(muduo::net::EventLoop* loop,
                uint64_t nodeId,
                const std::unordered_map<uint64_t, std::string>& peers,
                std::shared_ptr<RocksEngine> engine);

    void start();
    void stop();

    // 客户端读写接口
    bool propose(const std::string& command);
    bool isLeader() const;
    uint64_t getLeaderId() const;
    bool getLeaderAddr(std::string& addr) const;
    uint64_t getNodeId() const { return nodeId_; }

    // 定时 tick，由 EventLoop timer 驱动
    void tick();

private:
    // 对等节点 RPC
    void connectToPeer(uint64_t peerId, const std::string& addr);
    void sendRaftRpc(uint64_t peerId, const dkv::raft::RaftMessage& msg);

    // 收到对等节点的 Raft RPC
    void onPeerMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf,
                       muduo::Timestamp ts,
                       uint64_t fromNode);

    // 处理 Raft 日志条目并应用到 RocksDB
    void onApply(uint64_t index, const std::string& cmd);

    // 持久化 Raft 状态到 RocksDB
    void persistRaftState();

    // 从 RocksDB 恢复 Raft 状态（启动时调用）
    void recoverRaftState();

    muduo::net::EventLoop* loop_;
    uint64_t nodeId_;
    std::unordered_map<uint64_t, std::string> peerAddrs_;

    std::shared_ptr<RocksEngine> engine_;
    std::unique_ptr<RaftNode> raft_;

    // 到对等节点的连接: peerId → TcpClient
    std::unordered_map<uint64_t, std::unique_ptr<muduo::net::TcpClient>> peerClients_;
    // 对等节点的连接对象（用于发送）: peerId → TcpConnectionPtr
    std::unordered_map<uint64_t, muduo::net::TcpConnectionPtr> peerConns_;
    std::mutex peerMutex_;

    // 本地监听端口，供对等节点连接
    uint16_t raftPort_;
    std::unique_ptr<muduo::net::TcpServer> raftServer_;
};
