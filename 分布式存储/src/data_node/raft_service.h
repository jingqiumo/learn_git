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

// RaftService = RaftNode + 网络 + RocksDB 的胶水层
//
// 职责：把 Raft 算法的三个依赖（网络收发、磁盘持久化、状态机 apply）注入 RaftNode
//
//   RaftNode 的回调                     RaftService 的实现
//   ──────────────                      ──────────────────
//   persistCb_()           ──→    persistRaftState()     → engine_->put()
//   applyCb_(idx, cmd)     ──→    onApply(idx, cmd)      → engine_->put/del()
//   rpcSender_(peer, msg)  ──→    sendRaftRpc(peer, msg) → TcpClient 发送
//
class RaftService {
public:
    // nodeId: 本节点 ID
    // peers:  {peerId → "ip:port"} 映射表（含本节点），用于建立 TCP 连接和重定向
    // engine: RocksDB 实例（与 KvService 共享）
    RaftService(muduo::net::EventLoop* loop,
                uint64_t nodeId,
                const std::unordered_map<uint64_t, std::string>& peers,
                std::shared_ptr<RocksEngine> engine);

    // 启动：连接所有 peer + 恢复持久化状态
    void start();
    // 停止：断开所有 peer 连接
    void stop();

    // ---- 客户端 API（被 KvService 调用） ----
    // 提交一条命令到 Raft 日志（内部调 raft_->propose()）
    bool propose(const std::string& command);
    // 本节点是否是 Leader
    bool isLeader() const;
    // 当前已知的 Leader 节点 ID
    uint64_t getLeaderId() const;
    // 获取 Leader 的 "ip:port" 地址（Follower 被读写时用于返回重定向地址）
    bool getLeaderAddr(std::string& addr) const;
    uint64_t getNodeId() const { return nodeId_; }

    // 定时 tick，由 EventLoop 的定时器每 10ms 调用一次，驱动 raft_->tick()
    void tick();

private:
    // ---- 节点间 RPC 通信 ----
    // 向 peerId 发起 TCP 连接，addr 格式 "ip:port"
    void connectToPeer(uint64_t peerId, const std::string& addr);
    // 发送一条 Raft RPC 消息（RequestVote / AppendEntries）到指定 peer
    void sendRaftRpc(uint64_t peerId, const dkv::raft::RaftMessage& msg);
    // 收到 peer 发来的 Raft RPC 消息，解析 type 后分发给 raft_->handleXXX()
    void onPeerMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf,
                       muduo::Timestamp ts,
                       uint64_t fromNode);

    // ---- Raft → RocksDB 桥接 ----
    // Raft 日志被 commit 后，解析命令并写入 RocksDB（被 raft_ 的 applyCb_ 回调触发）
    void onApply(uint64_t index, const std::string& cmd);

    // ---- 持久化 ----
    // 将 currentTerm / votedFor / log[] 保存到 RocksDB（被 raft_ 的 persistCb_ 回调触发）
    void persistRaftState();
    // 启动时从 RocksDB 恢复 persistent state，传给 raft_->loadPersistedState()
    void recoverRaftState();

    // ==================== 成员变量 ====================

    // ---- 基础信息 ----
    muduo::net::EventLoop* loop_;                               // muduo 事件循环
    uint64_t nodeId_;                                           // 本节点 ID
    std::unordered_map<uint64_t, std::string> peerAddrs_;        // {peerId → "ip:port"} 地址簿

    // ---- 核心组件 ----
    std::shared_ptr<RocksEngine> engine_;    // RocksDB 实例（与 KvService 共享）
    std::unique_ptr<RaftNode> raft_;         // Raft 共识算法实例

    // ---- 对等节点连接 ----
    // TcpClient 管理 TCP 连接的生命周期（重连、断开检测）
    std::unordered_map<uint64_t, std::unique_ptr<muduo::net::TcpClient>> peerClients_;
    // 已建立的 TcpConnection，用于发送 Raft RPC 数据
    std::unordered_map<uint64_t, muduo::net::TcpConnectionPtr> peerConns_;
    // 保护 peerConns_ 的并发访问（连接回调来自 I/O 线程，tick 可能来自定时器线程）
    std::mutex peerMutex_;

    // ---- Raft 内部 RPC 监听端口（当前未使用，Raft RPC 复用了 API 端口） ----
    uint16_t raftPort_;
    std::unique_ptr<muduo::net::TcpServer> raftServer_;
};
