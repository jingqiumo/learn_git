#include "router.h"
#include <iostream>

void Router::setDirectNode(const std::string& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    directAddr_ = addr;
    usePd_ = false;
}

void Router::loadShardMap(const dkv::common::ShardMap& map) {
    std::lock_guard<std::mutex> lock(mutex_);
    shardMap_ = map;
    usePd_ = true;
}

std::string Router::getAddrForKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usePd_) return directAddr_;

    // Range-based 查找
    for (auto& shard : shardMap_.shards()) {
        bool afterStart = shard.start_key().empty() || key >= shard.start_key();
        bool beforeEnd = shard.end_key().empty() || key < shard.end_key();
        if (afterStart && beforeEnd) {
            auto it = nodeAddrs_.find(shard.leader_node_id());
            if (it != nodeAddrs_.end()) return it->second;
            return "";
        }
    }
    return "";
}

std::string Router::getDirectAddr() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return directAddr_;
}

void Router::updateLeader(uint64_t shardId, uint64_t nodeId, const std::string& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodeAddrs_[nodeId] = addr;
}

uint64_t Router::getShardId(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& shard : shardMap_.shards()) {
        bool afterStart = shard.start_key().empty() || key >= shard.start_key();
        bool beforeEnd = shard.end_key().empty() || key < shard.end_key();
        if (afterStart && beforeEnd) return shard.shard_id();
    }
    return 0;
}

std::string Router::getLeaderAddr(uint64_t shardId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& shard : shardMap_.shards()) {
        if (shard.shard_id() == shardId) {
            auto it = nodeAddrs_.find(shard.leader_node_id());
            if (it != nodeAddrs_.end()) return it->second;
        }
    }
    return "";
}
