#pragma once

#include "rocks_engine.h"
#include "raft_service.h"
#include "kv_rpc.pb.h"
#include "common.pb.h"
#include <muduo/net/TcpConnection.h>
#include <memory>

class KvService {
public:
    // Phase 1: 直接操作 RocksDB（无 Raft）
    KvService(std::shared_ptr<RocksEngine> engine, RaftService* raft);

    void handleGet(const muduo::net::TcpConnectionPtr& conn,
                   const dkv::kv::GetRequest& req);
    void handlePut(const muduo::net::TcpConnectionPtr& conn,
                   const dkv::kv::PutRequest& req);
    void handleDelete(const muduo::net::TcpConnectionPtr& conn,
                      const dkv::kv::DeleteRequest& req);
    void handleScan(const muduo::net::TcpConnectionPtr& conn,
                    const dkv::kv::ScanRequest& req);

private:
    std::shared_ptr<RocksEngine> engine_;
    RaftService* raft_;  // nullable (Phase 1 == nullptr)
};
