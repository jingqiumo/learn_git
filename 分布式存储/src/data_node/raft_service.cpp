#include "raft_service.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

using namespace muduo::net;
using muduo::Timestamp;

// ==================== RaftService ====================

RaftService::RaftService(EventLoop* loop,
                         uint64_t nodeId,
                         const std::unordered_map<uint64_t, std::string>& peers,
                         std::shared_ptr<RocksEngine> engine)
    : loop_(loop)
    , nodeId_(nodeId)
    , peerAddrs_(peers)
    , engine_(std::move(engine))
{
    // 创建 RaftNode
    raft_ = std::make_unique<RaftNode>(
        nodeId,
        [&peers]() {
            std::vector<uint64_t> ids;
            for (auto& p : peers) ids.push_back(p.first);
            return ids;
        }(),
        std::bind(&RaftService::persistRaftState, this),
        std::bind(&RaftService::onApply, this,
                  std::placeholders::_1, std::placeholders::_2));

    raft_->setRpcSender(
        [this](uint64_t peerId, const dkv::raft::RaftMessage& msg) {
            sendRaftRpc(peerId, msg);
        });

    recoverRaftState();
}

void RaftService::start() {
    // 连接到所有对等节点
    for (auto& [peerId, addr] : peerAddrs_) {
        if (peerId == nodeId_) continue;
        connectToPeer(peerId, addr);
        std::cout << "[Raft] Node " << nodeId_ << " connecting to peer "
                  << peerId << " at " << addr << std::endl;
    }
}

void RaftService::stop() {
    for (auto& [_, client] : peerClients_) {
        if (client) client->disconnect();
    }
}

void RaftService::connectToPeer(uint64_t peerId, const std::string& addr) {
    // addr format: "ip:port"
    size_t colon = addr.find(':');
    if (colon == std::string::npos) return;
    std::string ip = addr.substr(0, colon);
    uint16_t port = static_cast<uint16_t>(std::stoi(addr.substr(colon + 1)));

    auto client = std::make_unique<TcpClient>(loop_, InetAddress(ip, port),
                                              "raft-peer-" + std::to_string(peerId));
    client->setConnectionCallback(
        [this, peerId](const TcpConnectionPtr& conn) {
            if (conn->connected()) {
                std::lock_guard<std::mutex> lock(peerMutex_);
                peerConns_[peerId] = conn;
                std::cout << "[Raft] Connected to peer " << peerId << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(peerMutex_);
                peerConns_.erase(peerId);
                std::cout << "[Raft] Disconnected from peer " << peerId << std::endl;
            }
        });
    client->setMessageCallback(
        [this, peerId](const TcpConnectionPtr& conn, Buffer* buf, Timestamp ts) {
            onPeerMessage(conn, buf, ts, peerId);
        });
    client->connect();
    peerClients_[peerId] = std::move(client);
}

void RaftService::sendRaftRpc(uint64_t peerId, const dkv::raft::RaftMessage& msg) {
    std::lock_guard<std::mutex> lock(peerMutex_);
    auto it = peerConns_.find(peerId);
    if (it == peerConns_.end() || !it->second || !it->second->connected()) {
        return;
    }

    std::string data;
    msg.SerializeToString(&data);

    uint32_t len = htonl(data.size());
    Buffer buf;
    buf.append(&len, 4);
    buf.append(data);
    it->second->send(&buf);
}

void RaftService::onPeerMessage(const TcpConnectionPtr& conn,
                                 Buffer* buf, Timestamp ts, uint64_t fromNode) {
    (void)conn; (void)ts;
    while (buf->readableBytes() >= 4) {
        uint32_t len = 0;
        memcpy(&len, buf->peek(), 4);
        len = ntohl(len);
        if (buf->readableBytes() < 4 + len) break;

        buf->retrieve(4);
        std::string payload = buf->retrieveAsString(len);

        dkv::raft::RaftMessage msg;
        if (!msg.ParseFromString(payload)) continue;

        switch (msg.type()) {
        case dkv::raft::RaftMessage::REQUEST_VOTE: {
            auto resp = raft_->handleRequestVote(msg.request_vote());
            dkv::raft::RaftMessage rmsg;
            rmsg.set_type(dkv::raft::RaftMessage::REQUEST_VOTE_RESP);
            *rmsg.mutable_request_vote_resp() = resp;
            sendRaftRpc(fromNode, rmsg);
            break;
        }
        case dkv::raft::RaftMessage::REQUEST_VOTE_RESP: {
            auto& resp = msg.request_vote_resp();
            if (resp.term() > raft_->getCurrentTerm()) {
                // term 过期，handleRequestVote 中已处理
            }
            if (resp.vote_granted()) {
                std::cout << "[Raft] Node " << nodeId_
                          << " received vote from " << fromNode << std::endl;
            }
            break;
        }
        case dkv::raft::RaftMessage::APPEND_ENTRIES: {
            auto resp = raft_->handleAppendEntries(msg.append_entries());
            dkv::raft::RaftMessage rmsg;
            rmsg.set_type(dkv::raft::RaftMessage::APPEND_ENTRIES_RESP);
            *rmsg.mutable_append_entries_resp() = resp;
            sendRaftRpc(fromNode, rmsg);
            break;
        }
        case dkv::raft::RaftMessage::APPEND_ENTRIES_RESP: {
            // matchIndex/nextIndex 已在 RaftNode 内部维护
            // tick 中的 advanceCommit 会根据 matchIndex 推进
            break;
        }
        default:
            break;
        }
    }
}

// ==================== Raft → RocksDB ====================

void RaftService::onApply(uint64_t index, const std::string& cmd) {
    // cmd 是 PutRequest 或 DeleteRequest 的序列化数据
    // 解析为 RpcMessage（仅客户端写请求会被 propose）
    dkv::kv::RpcMessage msg;
    if (msg.ParseFromString(cmd)) {
        switch (msg.type()) {
        case dkv::kv::RpcMessage::PUT_REQUEST:
            engine_->put(msg.put_request().key(), msg.put_request().value());
            break;
        case dkv::kv::RpcMessage::DELETE_REQUEST:
            engine_->del(msg.delete_request().key());
            break;
        default:
            break;
        }
    }
}

void RaftService::persistRaftState() {
    // 将 Raft 持久化状态保存到 RocksDB
    // 使用特殊 key 前缀 0x00 存储 currentTerm / votedFor
    std::string termKey = std::string("\x00raft_term", 10);
    std::string voteKey = std::string("\x00raft_vote", 10);
    engine_->put(termKey, std::to_string(raft_->getCurrentTerm()));
    engine_->put(voteKey, std::to_string(raft_->getVotedFor()));
    // Note: 完整的 Log 持久化需要保存 log entries，此处简化
}

void RaftService::recoverRaftState() {
    // 从 RocksDB 恢复 Raft 状态（如果有的话）
    std::string termKey = std::string("\x00raft_term", 10);
    std::string termVal;
    if (engine_->get(termKey, termVal)) {
        uint64_t term = std::stoull(termVal);
        std::cout << "[Raft] Recovered term=" << term << std::endl;
    }
}

// ==================== 客户端 API ====================

bool RaftService::propose(const std::string& command) {
    return raft_->propose(command);
}

bool RaftService::isLeader() const {
    return raft_->isLeader();
}

uint64_t RaftService::getLeaderId() const {
    return raft_->getLeaderId();
}

bool RaftService::getLeaderAddr(std::string& addr) const {
    uint64_t lid = raft_->getLeaderId();
    if (lid == 0) return false;
    if (lid == nodeId_) return false; // 自己就是 leader
    auto it = peerAddrs_.find(lid);
    if (it == peerAddrs_.end()) return false;
    addr = it->second;
    return true;
}

void RaftService::tick() {
    raft_->tick();
}
