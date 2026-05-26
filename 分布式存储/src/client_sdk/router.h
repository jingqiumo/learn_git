#pragma once

#include "common.pb.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

class Router {
public:
    Router() = default;

    // Phase 1: 直连模式
    void setDirectNode(const std::string& addr);

    // Phase 3: PD 模式 - 加载完整 ShardMap
    void loadShardMap(const dkv::common::ShardMap& map);

    // 根据 key 获取 leader 地址
    std::string getAddrForKey(const std::string& key) const;

    // 获取 PD 地址
    void setPdAddr(const std::string& addr) { pdAddr_ = addr; }
    std::string getPdAddr() const { return pdAddr_; }

    // 获取 shard leader 地址（Phase 1 始终返回直连地址）
    std::string getDirectAddr() const;

    // 根据 shard_id 更新 leader 地址（处理重定向）
    void updateLeader(uint64_t shardId, uint64_t nodeId, const std::string& addr);

    // 根据 key 查找 shard_id
    uint64_t getShardId(const std::string& key) const;

    // 获取 shard leader 的地址
    std::string getLeaderAddr(uint64_t shardId) const;

private:
    mutable std::mutex mutex_;
    bool usePd_ = false;
    std::string directAddr_;
    std::string pdAddr_;

    dkv::common::ShardMap shardMap_;
    std::unordered_map<uint64_t, std::string> nodeAddrs_; // nodeId -> addr
};
