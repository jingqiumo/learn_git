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

// 简化版 Raft 实现
// 支持：Leader 选举、日志复制、commit 推进、状态持久化
// 简化：无 snapshot、无 membership change、无 prevote
class RaftNode {
public:
    enum State { FOLLOWER = 0, CANDIDATE = 1, LEADER = 2 };

    // 持久化回调：Raft 需要保存 currentTerm / votedFor / log
    using PersistCallback = std::function<void()>;
    // 应用回调：已提交的日志条目应用到状态机
    using ApplyCallback = std::function<void(uint64_t index, const std::string& cmd)>;
    // 发送 RPC 到对端节点
    using RpcSender = std::function<void(uint64_t peerId, const dkv::raft::RaftMessage& msg)>;

    RaftNode(uint64_t nodeId,
             const std::vector<uint64_t>& peerIds,
             PersistCallback persistCb,
             ApplyCallback applyCb);

    // 客户端提交命令（仅 Leader 可调用），返回是否成功
    bool propose(const std::string& command);

    // 处理收到的 Raft RPC
    dkv::raft::RequestVoteResponse handleRequestVote(const dkv::raft::RequestVoteRequest& req);
    dkv::raft::AppendEntriesResponse handleAppendEntries(const dkv::raft::AppendEntriesRequest& req);

    // 定时 tick，驱动选举超时和心跳
    void tick();

    // 查询接口
    bool isLeader() const { return state_ == LEADER; }
    uint64_t getLeaderId() const { return leaderId_; }
    uint64_t getNodeId() const { return nodeId_; }
    State getState() const { return state_; }
    uint64_t getCurrentTerm() const { return currentTerm_; }
    uint64_t getCommitIndex() const { return commitIndex_; }
    uint64_t getVotedFor() const { return votedFor_; }
    void setRpcSender(RpcSender s) { rpcSender_ = std::move(s); }

    // 网络断开后重连：reset election timer
    void resetElectionTimer();

private:
    void becomeFollower(uint64_t term);
    void becomeCandidate();
    void becomeLeader();
    void startElection();
    void sendAppendEntries(bool heartbeatOnly);
    void advanceCommitIndex();
    void applyEntries();

    // 持久化
    void persist();

    // 日志索引从 1 开始，log_[0] 是哨兵 (index=0, term=0)
    const dkv::raft::LogEntry& getLogEntry(uint64_t index) const;
    uint64_t getLastLogIndex() const;
    uint64_t getLastLogTerm() const;

    // ===== 持久化状态 =====
    uint64_t currentTerm_ = 0;
    uint64_t votedFor_ = 0;   // 0 表示未投票
    std::vector<dkv::raft::LogEntry> log_;

    // ===== 易失状态 =====
    State state_ = FOLLOWER;
    uint64_t commitIndex_ = 0;
    uint64_t lastApplied_ = 0;
    uint64_t leaderId_ = 0;

    // ===== Leader 状态 =====
    std::unordered_map<uint64_t, uint64_t> nextIndex_;
    std::unordered_map<uint64_t, uint64_t> matchIndex_;

    // ===== 选举状态 =====
    std::chrono::steady_clock::time_point lastHeartbeat_;
    int electionTimeoutMs_ = 300;
    int votesReceived_ = 0;
    uint64_t nodeId_;
    std::vector<uint64_t> peerIds_;

    PersistCallback persistCb_;
    ApplyCallback applyCb_;
    RpcSender rpcSender_;
    std::mt19937 rng_;
    std::mutex mutex_;

    int randomTimeout();
};
