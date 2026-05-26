#include "shard_manager.h"
#include <iostream>
#include <algorithm>
#include <ctime>

ShardManager::ShardManager() {}

uint64_t ShardManager::registerNode(const dkv::common::NodeInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t id = nextNodeId_++;
    NodeState ns;
    ns.nodeId = id;
    ns.ip = info.ip();
    ns.port = info.port();
    ns.alive = true;
    ns.lastHeartbeat = std::time(nullptr);
    nodes_[id] = ns;
    std::cout << "[PD] Node " << id << " registered (" << ns.ip << ":" << ns.port << ")" << std::endl;
    return id;
}

void ShardManager::processHeartbeat(uint64_t nodeId,
                                     const dkv::pd::HeartbeatRequest::ShardState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return;
    it->second.alive = true;
    it->second.lastHeartbeat = std::time(nullptr);
    it->second.leaderCount = state.is_leader() ? 1 : 0; // 简化
}

std::vector<uint64_t> ShardManager::getDeadNodes() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> dead;
    auto now = std::time(nullptr);
    for (auto& [id, ns] : nodes_) {
        if (ns.alive && now - ns.lastHeartbeat > 10) {
            ns.alive = false;
            dead.push_back(id);
            std::cout << "[PD] Node " << id << " is DEAD" << std::endl;
        }
    }
    return dead;
}

void ShardManager::initShards(int numShards, int replicaCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    shards_.clear();

    // 简单的 range-based 分片：按首字符分割
    // 实际系统会用一致性哈希或动态范围分割
    for (int i = 0; i < numShards; ++i) {
        ShardState shard;
        shard.shardId = i + 1;
        shard.startKey = std::string(1, static_cast<char>('a' + i * (26 / numShards)));
        shard.endKey = (i + 1 < numShards)
            ? std::string(1, static_cast<char>('a' + (i + 1) * (26 / numShards)))
            : "";
        shards_.push_back(shard);
    }
    std::cout << "[PD] Initialized " << numShards << " shards" << std::endl;
}

dkv::common::ShardMap ShardManager::getShardMap() const {
    std::lock_guard<std::mutex> lock(mutex_);
    dkv::common::ShardMap map;
    map.set_version(1);
    for (auto& s : shards_) {
        auto* meta = map.add_shards();
        meta->set_shard_id(s.shardId);
        meta->set_start_key(s.startKey);
        meta->set_end_key(s.endKey);
        meta->set_epoch(s.epoch);
        meta->set_leader_node_id(s.leaderNodeId);
        for (auto pid : s.peerNodeIds) {
            meta->add_peer_node_ids(pid);
        }
    }
    return map;
}

void ShardManager::assignShard(uint64_t shardId, uint64_t nodeId, bool isLeader) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : shards_) {
        if (s.shardId == shardId) {
            if (isLeader) s.leaderNodeId = nodeId;
            if (std::find(s.peerNodeIds.begin(), s.peerNodeIds.end(), nodeId)
                == s.peerNodeIds.end()) {
                s.peerNodeIds.push_back(nodeId);
            }
            return;
        }
    }
}

void ShardManager::scheduleRebalance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shards_.empty() || nodes_.empty()) return;

    // 简化：将 shard 均匀分配给 alive 节点
    std::vector<uint64_t> aliveNodes;
    for (auto& [id, ns] : nodes_) {
        if (ns.alive) aliveNodes.push_back(id);
    }
    if (aliveNodes.empty()) return;

    // 统计每个节点的 shard 数量
    std::unordered_map<uint64_t, int> shardCounts;
    for (auto& nid : aliveNodes) shardCounts[nid] = 0;
    for (auto& s : shards_) {
        for (auto pid : s.peerNodeIds) {
            if (shardCounts.count(pid)) shardCounts[pid]++;
        }
    }

    // 为没有 leader 的 shard 分配 leader
    for (auto& s : shards_) {
        if (s.leaderNodeId == 0 && !s.peerNodeIds.empty()) {
            s.leaderNodeId = s.peerNodeIds[0];
        }
    }

    // 为没有副本的 shard 分配副本到最少负载的节点
    for (auto& s : shards_) {
        while (s.peerNodeIds.size() < 3 && s.peerNodeIds.size() < aliveNodes.size()) {
            uint64_t best = aliveNodes[0];
            int bestCount = shardCounts[best];
            for (auto& nid : aliveNodes) {
                if (shardCounts[nid] < bestCount
                    && std::find(s.peerNodeIds.begin(), s.peerNodeIds.end(), nid)
                       == s.peerNodeIds.end()) {
                    best = nid;
                    bestCount = shardCounts[nid];
                }
            }
            s.peerNodeIds.push_back(best);
            shardCounts[best]++;
            std::cout << "[PD] Assigned shard " << s.shardId
                      << " replica to node " << best << std::endl;
        }
    }
}

void ShardManager::setInitialNodes(
        const std::vector<uint64_t>& nodeIds,
        const std::unordered_map<uint64_t, std::string>& addrs) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodeAddrs_ = addrs;
    for (auto id : nodeIds) {
        NodeState ns;
        ns.nodeId = id;
        ns.alive = true;
        nodes_[id] = ns;
        if (id >= nextNodeId_) nextNodeId_ = id + 1;
    }
}
