#pragma once

#include "router.h"
#include "kv_rpc.pb.h"
#include "pd_rpc.pb.h"
#include <string>
#include <vector>
#include <cstdint>

class KvClient {
public:
    KvClient();
    ~KvClient();

    // Phase 1: 直连单节点
    bool connect(const std::string& ip, uint16_t port);
    void close();

    // Phase 3: 连接 PD，获取路由表
    bool connectToPd(const std::string& pdIp, uint16_t pdPort);

    // KV API（自动路由 + 重定向重试）
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);
    std::vector<std::pair<std::string, std::string>>
        scan(const std::string& start_key,
             const std::string& end_key,
             uint32_t limit = 100);

    void setRouter(Router* router);

private:
    bool sendToAddr(const std::string& addr,
                    const dkv::kv::RpcMessage& req,
                    dkv::kv::RpcMessage& resp,
                    int maxRetries = 3);

    bool sendRaw(const std::string& ip, uint16_t port,
                 const std::string& data, std::string& respData);

    // 从 PD 拉取 shard map
    bool fetchShardMap();

    int pdSock_ = -1;
    Router defaultRouter_;
    Router* router_ = nullptr;
    bool usePd_ = false;
};
