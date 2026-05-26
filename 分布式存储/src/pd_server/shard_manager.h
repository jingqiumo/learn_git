#pragma once

#include "common.pb.h"
#include "pd_rpc.pb.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

// 简化的 PD 元数据管理器
// 维护：node 注册信息、shard 分配、心跳状态
class ShardManager {
public:
    ShardManager();

    // ===== 节点管理 =====
    uint64_t registerNode(const dkv::common::NodeInfo& node);
    void processHeartbeat(uint64_t nodeId,
                          const dkv::pd::HeartbeatRequest::ShardState& state);
    std::vector<uint64_t> getDeadNodes();

    // ===== Shard 管理 =====
    void initShards(int numShards, int replicaCount);
    dkv::common::ShardMap getShardMap() const;
    void assignShard(uint64_t shardId, uint64_t nodeId, bool isLeader);
    void scheduleRebalance();

    // ===== 初始集群配置 =====
    void setInitialNodes(const std::vector<uint64_t>& nodeIds,
                         const std::unordered_map<uint64_t, std::string>& addrs);

private:
    mutable std::mutex mutex_;

    // 节点信息
    struct NodeState {
        uint64_t nodeId;
        std::string ip;
        uint32_t port;
        uint32_t shardCount = 0;
        uint32_t leaderCount = 0;
        bool alive = true;
        int64_t lastHeartbeat = 0;
    };
    std::unordered_map<uint64_t, NodeState> nodes_;
    uint64_t nextNodeId_ = 1;

    // Shard 信息
    struct ShardState {
        uint64_t shardId;
        std::string startKey;
        std::string endKey;
        uint64_t epoch = 1;
        uint64_t leaderNodeId = 0;
        std::vector<uint64_t> peerNodeIds;
    };
    std::vector<ShardState> shards_;

    // 地址簿
    std::unordered_map<uint64_t, std::string> nodeAddrs_;
};
