#pragma once

#include "shard_manager.h"
#include "pd_rpc.pb.h"
#include "kv_rpc.pb.h"
#include <muduo/net/TcpConnection.h>

class PdService {
public:
    explicit PdService(ShardManager* mgr) : mgr_(mgr) {}

    void handleRegisterNode(const muduo::net::TcpConnectionPtr& conn,
                            const dkv::pd::RegisterNodeRequest& req);

    void handleHeartbeat(const muduo::net::TcpConnectionPtr& conn,
                         const dkv::pd::HeartbeatRequest& req);

    void handleGetShardMap(const muduo::net::TcpConnectionPtr& conn,
                           const dkv::pd::GetShardMapRequest& req);

private:
    ShardManager* mgr_;
};
