#include "pd_service.h"
#include "codec.h"

using namespace muduo::net;

void PdService::handleRegisterNode(const TcpConnectionPtr& conn,
                                    const dkv::pd::RegisterNodeRequest& req) {
    uint64_t id = mgr_->registerNode(req.node());

    dkv::pd::RegisterNodeResponse resp;
    resp.set_error_code(dkv::common::OK);
    resp.set_assigned_node_id(id);

    // 用 RpcMessage 包裹（通过 kv_rpc 的扩展类型发送 PD 响应）
    // 简化：直接序列化 RegisterNodeResponse 发送
    std::string data;
    resp.SerializeToString(&data);
    uint32_t len = htonl(data.size());
    muduo::net::Buffer buf;
    buf.append(&len, 4);
    buf.append(data);
    conn->send(&buf);
}

void PdService::handleHeartbeat(const TcpConnectionPtr& conn,
                                 const dkv::pd::HeartbeatRequest& req) {
    for (auto& state : req.shard_states()) {
        mgr_->processHeartbeat(req.node_id(), state);
    }

    dkv::pd::HeartbeatResponse resp;
    resp.set_error_code(dkv::common::OK);

    std::string data;
    resp.SerializeToString(&data);
    uint32_t len = htonl(data.size());
    muduo::net::Buffer buf;
    buf.append(&len, 4);
    buf.append(data);
    conn->send(&buf);
}

void PdService::handleGetShardMap(const TcpConnectionPtr& conn,
                                   const dkv::pd::GetShardMapRequest& req) {
    (void)req;
    auto map = mgr_->getShardMap();

    dkv::pd::GetShardMapResponse resp;
    resp.set_error_code(dkv::common::OK);
    *resp.mutable_shard_map() = map;

    std::string data;
    resp.SerializeToString(&data);
    uint32_t len = htonl(data.size());
    muduo::net::Buffer buf;
    buf.append(&len, 4);
    buf.append(data);
    conn->send(&buf);
}
