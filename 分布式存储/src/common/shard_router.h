#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct ShardMeta {
    uint64_t shard_id;
    std::string start_key;
    std::string end_key;
    uint64_t epoch;
    uint64_t leader_node_id;
    std::vector<uint64_t> peer_node_ids;
};

struct ShardMap {
    std::vector<ShardMeta> shards;
    uint64_t version = 0;
};

// Range-based 分片路由器
// 每个 shard 负责一个半开区间 [start_key, end_key)
class ShardRouter {
public:
    ShardRouter() = default;

    // 加载 PD 返回的 shard map
    void loadShardMap(const ShardMap& map);

    // 根据 key 查找对应的 shard_id
    // 返回 0 表示未找到
    uint64_t getShardId(const std::string& key) const;

    // 获取 shard 的 leader 地址（格式: "ip:port"）
    // 需要在 shard 信息中包含地址，这里单独维护一个映射
    void updateLeader(uint64_t shard_id, uint64_t node_id, const std::string& addr);
    std::string getLeaderAddr(uint64_t shard_id) const;

    // 获取 shard 的 leader node_id
    uint64_t getLeaderNodeId(uint64_t shard_id) const;

    const ShardMap& getShardMap() const { return map_; }

private:
    ShardMap map_;
    // shard_id -> (node_id -> addr)
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::string>> node_addrs_;
};
