#include "raft_node.h"
#include <algorithm>
#include <iostream>

RaftNode::RaftNode(uint64_t nodeId,
                   const std::vector<uint64_t>& peerIds,
                   PersistCallback persistCb,
                   ApplyCallback applyCb)
    : nodeId_(nodeId)
    , peerIds_(peerIds)
    , persistCb_(std::move(persistCb))
    , applyCb_(std::move(applyCb))
    , rng_(std::random_device{}())
{
    // 日志哨兵: index=0, term=0
    dkv::raft::LogEntry sentinel;
    sentinel.set_term(0);
    sentinel.set_index(0);
    log_.push_back(sentinel);

    lastHeartbeat_ = std::chrono::steady_clock::now();
    electionTimeoutMs_ = randomTimeout();

    std::cout << "[Raft] Node " << nodeId_ << " started, timeout="
              << electionTimeoutMs_ << "ms" << std::endl;
}

// ==================== 公开接口 ====================

bool RaftNode::propose(const std::string& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != LEADER) return false;

    dkv::raft::LogEntry entry;
    entry.set_term(currentTerm_);
    entry.set_index(getLastLogIndex() + 1);
    entry.set_command(command);
    log_.push_back(entry);

    matchIndex_[nodeId_] = entry.index();
    nextIndex_[nodeId_] = entry.index() + 1;

    persist();
    sendAppendEntries(false);
    return true;
}

dkv::raft::RequestVoteResponse
RaftNode::handleRequestVote(const dkv::raft::RequestVoteRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);

    dkv::raft::RequestVoteResponse resp;

    if (req.term() > currentTerm_) {
        becomeFollower(req.term());
    }

    resp.set_term(currentTerm_);

    bool grant = false;
    if (req.term() >= currentTerm_
        && (votedFor_ == 0 || votedFor_ == req.candidate_id())
        && (req.last_log_term() > getLastLogTerm()
            || (req.last_log_term() == getLastLogTerm()
                && req.last_log_index() >= getLastLogIndex()))) {
        grant = true;
        votedFor_ = req.candidate_id();
        lastHeartbeat_ = std::chrono::steady_clock::now();
        persist();
    }

    resp.set_vote_granted(grant);
    return resp;
}

void RaftNode::processRequestVoteResponse(uint64_t fromPeer, uint64_t respTerm,
                                          bool voteGranted) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 更高 term → 退位，与论文 Figure 2 一致
    if (respTerm > currentTerm_) {
        becomeFollower(respTerm);
        return;
    }
    // term 不匹配 或 已不是 Candidate → 忽略
    if (respTerm != currentTerm_ || state_ != CANDIDATE) return;

    if (voteGranted) {
        votesReceived_++;
        std::cout << "[Raft] Node " << nodeId_ << " got vote from " << fromPeer
                  << " (" << votesReceived_ << "/" << (peerIds_.size() + 1) << ")"
                  << std::endl;
    }

    int majority = (int)(peerIds_.size() + 1) / 2 + 1;
    if (votesReceived_ >= majority) {
        becomeLeader();
    }
}

void RaftNode::processAppendEntriesResponse(uint64_t fromPeer, uint64_t term,
                                             bool success, uint64_t matchIndex) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != LEADER || term != currentTerm_) return;

    if (success) {
        // Follower 确认复制到了 matchIndex，更新进度
        if (matchIndex > matchIndex_[fromPeer]) {
            matchIndex_[fromPeer] = matchIndex;
            nextIndex_[fromPeer] = matchIndex + 1;
        }
    } else {
        // 日志不一致，回退 nextIndex 下次重试
        if (nextIndex_[fromPeer] > 1) {
            nextIndex_[fromPeer]--;
        }
        sendAppendEntries(false);  // 立刻重试，不等下一个 tick
    }
}

dkv::raft::AppendEntriesResponse
RaftNode::handleAppendEntries(const dkv::raft::AppendEntriesRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);

    dkv::raft::AppendEntriesResponse resp;

    if (req.term() < currentTerm_) {
        resp.set_term(currentTerm_);
        resp.set_success(false);
        return resp;
    }

    // 收到合法的 AppendEntries → 重置选举计时器
    lastHeartbeat_ = std::chrono::steady_clock::now();

    if (req.term() > currentTerm_) {
        becomeFollower(req.term());
    }
    leaderId_ = req.leader_id();

    // 检查 log matching
    if (req.prev_log_index() > getLastLogIndex()) {
        resp.set_term(currentTerm_);
        resp.set_success(false);
        resp.set_match_index(getLastLogIndex());
        return resp;
    }

    if (req.prev_log_index() > 0
        && getLogEntry(req.prev_log_index()).term() != req.prev_log_term()) {
        resp.set_term(currentTerm_);
        resp.set_success(false);
        resp.set_match_index(req.prev_log_index() - 1);
        return resp;
    }

    // 删除冲突条目，追加新条目
    uint64_t idx = req.prev_log_index();
    for (int i = 0; i < req.entries_size(); ++i) {
        idx++;
        auto& e = req.entries(i);
        if (idx <= getLastLogIndex()
            && getLogEntry(idx).term() != e.term()) {
            // 截断
            log_.resize(idx);
        }
        if (idx > getLastLogIndex()) {
            log_.push_back(e);
        }
    }

    // 更新 commitIndex
    if (req.leader_commit() > commitIndex_) {
        commitIndex_ = std::min(req.leader_commit(), getLastLogIndex());
    }

    persist();
    applyEntries();

    resp.set_term(currentTerm_);
    resp.set_success(true);
    resp.set_match_index(getLastLogIndex());
    return resp;
}

void RaftNode::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastHeartbeat_).count();

    if (state_ == FOLLOWER || state_ == CANDIDATE) {
        if (elapsed >= electionTimeoutMs_) {
            if (state_ == FOLLOWER) {
                becomeCandidate();
            } else {
                startElection();
            }
        }
    }

    if (state_ == LEADER) {
        // 心跳间隔 50ms
        if (elapsed >= 50) {
            lastHeartbeat_ = now;
            sendAppendEntries(true);
        }
        advanceCommitIndex();
        applyEntries();
    }
}

// ==================== 状态转换 ====================

void RaftNode::becomeFollower(uint64_t term) {
    state_ = FOLLOWER;
    currentTerm_ = term;
    votedFor_ = 0;
    lastHeartbeat_ = std::chrono::steady_clock::now();
    electionTimeoutMs_ = randomTimeout();
    persist();
}

void RaftNode::becomeCandidate() {
    state_ = CANDIDATE;
    currentTerm_++;
    votedFor_ = nodeId_;
    leaderId_ = 0;
    votesReceived_ = 1;
    lastHeartbeat_ = std::chrono::steady_clock::now();
    electionTimeoutMs_ = randomTimeout();
    persist();
    startElection();
}

void RaftNode::becomeLeader() {
    state_ = LEADER;
    leaderId_ = nodeId_;
    std::cout << "[Raft] Node " << nodeId_ << " becomes LEADER for term "
              << currentTerm_ << std::endl;

    uint64_t lastIdx = getLastLogIndex();
    for (auto pid : peerIds_) {
        nextIndex_[pid] = lastIdx + 1;
        matchIndex_[pid] = 0;
    }
    nextIndex_[nodeId_] = lastIdx + 1;
    matchIndex_[nodeId_] = lastIdx;

    lastHeartbeat_ = std::chrono::steady_clock::now();
    sendAppendEntries(true);
}

void RaftNode::startElection() {
    votesReceived_ = 1;  // 重置票数，投自己一票
    std::cout << "[Raft] Node " << nodeId_ << " starting election for term "
              << currentTerm_ << std::endl;

    dkv::raft::RaftMessage msg;
    msg.set_type(dkv::raft::RaftMessage::REQUEST_VOTE);
    auto* req = msg.mutable_request_vote();
    req->set_term(currentTerm_);
    req->set_candidate_id(nodeId_);
    req->set_last_log_index(getLastLogIndex());
    req->set_last_log_term(getLastLogTerm());

    for (auto pid : peerIds_) {
        if (rpcSender_) rpcSender_(pid, msg);
    }
}

// ==================== Leader 心跳/复制 ====================

void RaftNode::sendAppendEntries(bool heartbeatOnly) {
    for (auto pid : peerIds_) {
        dkv::raft::RaftMessage msg;
        msg.set_type(dkv::raft::RaftMessage::APPEND_ENTRIES);
        auto* req = msg.mutable_append_entries();
        req->set_term(currentTerm_);
        req->set_leader_id(nodeId_);
        req->set_leader_commit(commitIndex_);

        uint64_t nextIdx = nextIndex_[pid];
        req->set_prev_log_index(nextIdx - 1);
        req->set_prev_log_term(getLogEntry(nextIdx - 1).term());

        if (!heartbeatOnly && nextIdx <= getLastLogIndex()) {
            // 发送从 nextIdx 开始的所有新条目
            for (uint64_t i = nextIdx; i <= getLastLogIndex(); ++i) {
                *req->add_entries() = log_[i];
            }
        }

        if (rpcSender_) rpcSender_(pid, msg);
    }
}

void RaftNode::advanceCommitIndex() {
    if (state_ != LEADER) return;

    // 找到被多数节点复制的最大 log index
    for (uint64_t n = getLastLogIndex(); n > commitIndex_; --n) {
        if (log_[n].term() != currentTerm_) continue;

        int count = 1; // leader itself
        for (auto pid : peerIds_) {
            if (matchIndex_[pid] >= n) count++;
        }
        if (count > (int)(peerIds_.size() + 1) / 2) {
            commitIndex_ = n;
            break;
        }
    }
}

void RaftNode::applyEntries() {
    while (lastApplied_ < commitIndex_) {
        lastApplied_++;
        if (lastApplied_ > 0 && applyCb_) {
            applyCb_(lastApplied_, log_[lastApplied_].command());
        }
    }
}

// ==================== 辅助函数 ====================

void RaftNode::persist() {
    if (persistCb_) persistCb_();
}

void RaftNode::loadPersistedState(uint64_t term, uint64_t votedFor,
                                   const std::vector<dkv::raft::LogEntry>& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentTerm_ = term;
    votedFor_ = votedFor;
    log_.clear();
    // 重新加哨兵
    dkv::raft::LogEntry sentinel;
    sentinel.set_term(0);
    sentinel.set_index(0);
    log_.push_back(sentinel);
    for (auto& e : log) log_.push_back(e);
    std::cout << "[Raft] Node " << nodeId_ << " recovered: term=" << currentTerm_
              << ", log size=" << (log_.size() - 1) << std::endl;
}

const dkv::raft::LogEntry& RaftNode::getLogEntry(uint64_t index) const {
    if (index < log_.size()) return log_[index];
    static dkv::raft::LogEntry empty;
    return empty;
}

uint64_t RaftNode::getLastLogIndex() const {
    return log_.empty() ? 0 : log_.size() - 1;
}

uint64_t RaftNode::getLastLogTerm() const {
    return log_.empty() ? 0 : log_.back().term();
}

void RaftNode::resetElectionTimer() {
    lastHeartbeat_ = std::chrono::steady_clock::now();
}

int RaftNode::randomTimeout() {
    std::uniform_int_distribution<int> dist(150, 300);
    return dist(rng_);
}
