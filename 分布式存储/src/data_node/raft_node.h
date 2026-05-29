#pragma once

#include "raft_rpc.pb.h"
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <random>
#include <chrono>
#include <mutex>
#include <cstdint>

// 简化版 Raft 共识算法实现
// 对照论文「In Search of an Understandable Consensus Algorithm」Figure 2
// 支持：Leader 选举、日志复制、commit 推进、持久化 + 重启恢复
// 简化：无 snapshot、无 membership change、无 prevote
class RaftNode {
public:
    // ---- 节点角色 ----
    enum State { FOLLOWER = 0, CANDIDATE = 1, LEADER = 2 };

    // ---- 依赖注入的回调函数类型 ----
    // RaftNode 不直接操作网络和磁盘，所有 I/O 通过这三个回调交给外层（RaftService）

    // 持久化回调：currentTerm / votedFor / log 变化时调用，外层负责写到磁盘
    using PersistCallback = std::function<void()>;
    // apply 回调：日志条目被 commit 后调用，外层负责执行到状态机（RocksDB put/del）
    // 参数: (日志index, 序列化的KV命令)
    using ApplyCallback = std::function<void(uint64_t index, const std::string& cmd)>;
    // RPC 发送回调：需要发 RequestVote / AppendEntries 给 peer 时调用
    // 参数: (目标节点ID, 序列化好的RaftMessage)
    using RpcSender = std::function<void(uint64_t peerId, const dkv::raft::RaftMessage& msg)>;

    // ---- 构造 ----
    // nodeId:  本节点 ID
    // peerIds: 集群其他节点的 ID 列表（不含自己）
    // persistCb / applyCb: 见上面类型定义
    RaftNode(uint64_t nodeId,
             const std::vector<uint64_t>& peerIds,
             PersistCallback persistCb,
             ApplyCallback applyCb);

    // ==================== 对外接口 ====================

    // ---- 客户端操作 ----
    // 提交一条命令到 Raft 日志。仅 Leader 可调用，Follower 调用返回 false。
    // command: 序列化后的 KV 操作（PutRequest 或 DeleteRequest 的 protobuf 字节）
    bool propose(const std::string& command);

    // ---- RPC 处理 ----
    // 收到投票请求：决定是否投票给对方
    dkv::raft::RequestVoteResponse handleRequestVote(const dkv::raft::RequestVoteRequest& req);
    // 收到日志复制/心跳：检查日志一致性，追加或截断
    dkv::raft::AppendEntriesResponse handleAppendEntries(const dkv::raft::AppendEntriesRequest& req);
    // Candidate 收到投票回复：累加票数，超过半数则成为 Leader
    void processRequestVoteResponse(uint64_t fromPeer, uint64_t term, bool voteGranted);
    // Leader 收到 AppendEntries 回复：更新 matchIndex/nextIndex，失败则递减后重试
    void processAppendEntriesResponse(uint64_t fromPeer, uint64_t term,
                                       bool success, uint64_t matchIndex);

    // ---- 定时驱动 ----
    // 每 10ms 由 muduo 定时器调用。Follower 检查选举超时，Leader 发心跳 + 推进 commit
    void tick();

    // ---- 查询接口（const，不加锁，调用方自己保证同步） ----
    bool isLeader() const { return state_ == LEADER; }       // 本节点是不是 Leader
    uint64_t getLeaderId() const { return leaderId_; }       // 当前已知的 Leader ID
    uint64_t getNodeId() const { return nodeId_; }           // 本节点 ID
    State getState() const { return state_; }                // 当前角色
    uint64_t getCurrentTerm() const { return currentTerm_; } // 当前任期号
    uint64_t getCommitIndex() const { return commitIndex_; } // 已多数确认的最大 index
    uint64_t getVotedFor() const { return votedFor_; }       // 当前 term 投给了谁（0=未投）
    void setRpcSender(RpcSender s) { rpcSender_ = std::move(s); } // 注入 RPC 发送回调

    // ---- 持久化与恢复 ----
    // 重启时从磁盘恢复 persistent state，直接设置 currentTerm/votedFor/log 三个变量
    void loadPersistedState(uint64_t term, uint64_t votedFor,
                            const std::vector<dkv::raft::LogEntry>& log);
    // 获取日志只读引用，供持久化层遍历并保存到磁盘
    const std::vector<dkv::raft::LogEntry>& getLog() const { return log_; }

    // ---- 连接管理 ----
    // 网络重连后重置选举超时计时器，防止不必要的选举
    void resetElectionTimer();

private:
    // ==================== 状态转换 ====================
    // 转为 Follower：清空 votedFor，重置选举超时（论文 Figure 2 Rules for Servers）
    void becomeFollower(uint64_t term);
    // 转为 Candidate：term+1，投自己一票，向所有 peer 发 RequestVote
    void becomeCandidate();
    // 转为 Leader：初始化 nextIndex/matchIndex，立刻发一轮心跳确立权威
    void becomeLeader();
    // 发起一轮选举：构造 RequestVote RPC 并发送给所有 peer
    void startElection();

    // ==================== Leader 操作 ====================
    // 发送 AppendEntries（heartbeatOnly=true 时空 entries = 纯心跳，false = 日志复制）
    void sendAppendEntries(bool heartbeatOnly);
    // 推进 commitIndex：找到被多数 peer 确认且属于当前 term 的最大 index
    void advanceCommitIndex();
    // 把 commitIndex 之后、lastApplied 之前的日志条目通过 applyCb_ 回调到状态机
    void applyEntries();

    // ==================== 持久化 ====================
    // 调用 persistCb_，通知外层把 persistent state 写入磁盘
    void persist();

    // ==================== 日志辅助 ====================
    // 日志从索引 1 开始，log_[0] 是哨兵 (index=0, term=0)，让数组下标对齐 Raft index
    const dkv::raft::LogEntry& getLogEntry(uint64_t index) const; // 按 index 取日志条目
    uint64_t getLastLogIndex() const;  // 最后一条日志的 index（= log_.size() - 1）
    uint64_t getLastLogTerm() const;   // 最后一条日志的 term
    int randomTimeout();               // 生成 150~300ms 的随机选举超时

    // ==================== 成员变量 ====================

    // ---- 持久化状态（崩溃后必须恢复，否则破坏 Raft 安全性） ----
    uint64_t currentTerm_ = 0;                     // 当前任期号，每次选举 +1
    uint64_t votedFor_ = 0;                        // 当前 term 投给了哪个节点，0 表示未投票
    std::vector<dkv::raft::LogEntry> log_;          // Raft 日志，log_[0] 是哨兵

    // ---- 易失状态（重启后归零，可从 Leader 恢复） ----
    State state_ = FOLLOWER;        // 当前角色：FOLLOWER / CANDIDATE / LEADER
    uint64_t commitIndex_ = 0;     // 已被多数确认的最大 log index
    uint64_t lastApplied_ = 0;     // 已 apply 到状态机的最大 log index
    uint64_t leaderId_ = 0;        // 当前已知的 Leader 节点 ID（Follower 用它知道 Leader 是谁）

    // ---- Leader 专用状态（跟踪每个 Follower 的复制进度） ----
    // nextIndex_[peer] = 下次发给该 peer 的日志起始 index（初始 = Leader 日志末尾 + 1）
    std::unordered_map<uint64_t, uint64_t> nextIndex_;
    // matchIndex_[peer] = 已知该 peer 已复制到的最大 index（初始 = 0）
    std::unordered_map<uint64_t, uint64_t> matchIndex_;

    // ---- 选举状态 ----
    std::chrono::steady_clock::time_point lastHeartbeat_; // 上次收到 Leader 消息的时间戳
    int electionTimeoutMs_ = 300;          // 本轮选举超时阈值（ms），随机 150~300
    int votesReceived_ = 0;               // 本轮选举收到的票数（含自己投的那票）
    uint64_t nodeId_;                     // 本节点 ID
    std::vector<uint64_t> peerIds_;       // 集群其他节点的 ID 列表（不含自己）

    // ---- 依赖注入的回调 ----
    PersistCallback persistCb_;   // 持久化回调 → RaftService::persistRaftState()
    ApplyCallback applyCb_;       // apply 回调 → RaftService::onApply()
    RpcSender rpcSender_;         // RPC 发送回调 → RaftService::sendRaftRpc()

    // ---- 工具 ----
    std::mt19937 rng_;     // 梅森旋转随机数生成器，用于生成随机选举超时
    std::mutex mutex_;      // 保护所有成员变量，tick/RPC 处理/propose 可能并发调用
};
