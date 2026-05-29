#include "shard_router.h"
#include <algorithm>

void ShardRouter::loadShardMap(const ShardMap& map) {
    map_ = map;
}

uint64_t ShardRouter::getShardId(const std::string& key) const {
    // Range-based 查找: 找到 start_key <= key < end_key 的 shard
    // 假设 shards 按 start_key 排序
    for (const auto& shard : map_.shards) {
        // 空 start_key 表示 -inf
        bool afterStart = shard.start_key.empty() || key >= shard.start_key;
        // 空 end_key 表示 +inf
        bool beforeEnd = shard.end_key.empty() || key < shard.end_key;
        if (afterStart && beforeEnd) {
            return shard.shard_id;
        }
    }
    return 0; // 未找到
}

void ShardRouter::updateLeader(uint64_t shard_id, uint64_t node_id,
                                const std::string& addr) {
    node_addrs_[shard_id][node_id] = addr;
}

std::string ShardRouter::getLeaderAddr(uint64_t shard_id) const {
    for (const auto& shard : map_.shards) {
        if (shard.shard_id == shard_id) {
            auto it = node_addrs_.find(shard_id);
            if (it != node_addrs_.end()) {
                auto it2 = it->second.find(shard.leader_node_id);
                if (it2 != it->second.end()) {
                    return it2->second;
                }
            }
        }
    }
    return "";
}

uint64_t ShardRouter::getLeaderNodeId(uint64_t shard_id) const {
    for (const auto& shard : map_.shards) {
        if (shard.shard_id == shard_id) {
            return shard.leader_node_id;
        }
    }
    return 0;
}
