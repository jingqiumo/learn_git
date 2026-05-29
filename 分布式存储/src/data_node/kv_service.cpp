#include "kv_service.h"
#include "codec.h"

using namespace muduo::net;

KvService::KvService(std::shared_ptr<RocksEngine> engine, RaftService* raft)
    : engine_(std::move(engine)), raft_(raft) {}

void KvService::handleGet(const TcpConnectionPtr& conn,
                           const dkv::kv::GetRequest& req) {
    dkv::kv::GetResponse resp;
    resp.set_call_id(req.call_id());
    resp.set_key(req.key());

    // 如果有 Raft 且本节点不是 Leader → 重定向
    if (raft_ && !raft_->isLeader()) {
        std::string leaderAddr;
        if (raft_->getLeaderAddr(leaderAddr)) {
            resp.set_error_code(dkv::common::NOT_LEADER);
            resp.set_correct_leader_addr(leaderAddr);
            dkv::kv::RpcMessage msg;
            msg.set_type(dkv::kv::RpcMessage::GET_RESPONSE);
            *msg.mutable_get_response() = resp;
            LengthPrefixedCodec::send(conn, msg);
            return;
        }
    }

    std::string value;
    if (engine_->get(req.key(), value)) {
        resp.set_error_code(dkv::common::OK);
        resp.set_value(value);
    } else {
        resp.set_error_code(dkv::common::KEY_NOT_FOUND);
    }

    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::GET_RESPONSE);
    *msg.mutable_get_response() = resp;
    LengthPrefixedCodec::send(conn, msg);
}

void KvService::handlePut(const TcpConnectionPtr& conn,
                           const dkv::kv::PutRequest& req) {
    dkv::kv::PutResponse resp;
    resp.set_call_id(req.call_id());

    if (raft_) {
        // Phase 2: 通过 Raft 提交
        if (!raft_->isLeader()) {
            std::string leaderAddr;
            if (raft_->getLeaderAddr(leaderAddr)) {
                resp.set_error_code(dkv::common::NOT_LEADER);
                resp.set_correct_leader_addr(leaderAddr);
                dkv::kv::RpcMessage msg;
                msg.set_type(dkv::kv::RpcMessage::PUT_RESPONSE);
                *msg.mutable_put_response() = resp;
                LengthPrefixedCodec::send(conn, msg);
                return;
            }
            resp.set_error_code(dkv::common::INTERNAL_ERROR);
        } else {
            // 序列化整个请求作为 Raft 命令
            dkv::kv::RpcMessage cmd;
            cmd.set_type(dkv::kv::RpcMessage::PUT_REQUEST);
            *cmd.mutable_put_request() = req;
            std::string serialized;
            cmd.SerializeToString(&serialized);

            if (raft_->propose(serialized)) {
                resp.set_error_code(dkv::common::OK);
            } else {
                resp.set_error_code(dkv::common::INTERNAL_ERROR);
            }
        }
    } else {
        // Phase 1: 直接写 RocksDB
        if (engine_->put(req.key(), req.value())) {
            resp.set_error_code(dkv::common::OK);
        } else {
            resp.set_error_code(dkv::common::INTERNAL_ERROR);
        }
    }

    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::PUT_RESPONSE);
    *msg.mutable_put_response() = resp;
    LengthPrefixedCodec::send(conn, msg);
}

void KvService::handleDelete(const TcpConnectionPtr& conn,
                              const dkv::kv::DeleteRequest& req) {
    dkv::kv::DeleteResponse resp;
    resp.set_call_id(req.call_id());

    if (raft_) {
        if (!raft_->isLeader()) {
            std::string leaderAddr;
            if (raft_->getLeaderAddr(leaderAddr)) {
                resp.set_error_code(dkv::common::NOT_LEADER);
                resp.set_correct_leader_addr(leaderAddr);
                dkv::kv::RpcMessage msg;
                msg.set_type(dkv::kv::RpcMessage::DELETE_RESPONSE);
                *msg.mutable_delete_response() = resp;
                LengthPrefixedCodec::send(conn, msg);
                return;
            }
            resp.set_error_code(dkv::common::INTERNAL_ERROR);
        } else {
            dkv::kv::RpcMessage cmd;
            cmd.set_type(dkv::kv::RpcMessage::DELETE_REQUEST);
            *cmd.mutable_delete_request() = req;
            std::string serialized;
            cmd.SerializeToString(&serialized);

            if (raft_->propose(serialized)) {
                resp.set_error_code(dkv::common::OK);
            } else {
                resp.set_error_code(dkv::common::INTERNAL_ERROR);
            }
        }
    } else {
        if (engine_->del(req.key())) {
            resp.set_error_code(dkv::common::OK);
        } else {
            resp.set_error_code(dkv::common::KEY_NOT_FOUND);
        }
    }

    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::DELETE_RESPONSE);
    *msg.mutable_delete_response() = resp;
    LengthPrefixedCodec::send(conn, msg);
}

void KvService::handleScan(const TcpConnectionPtr& conn,
                            const dkv::kv::ScanRequest& req) {
    dkv::kv::ScanResponse resp;
    resp.set_call_id(req.call_id());

    // 读操作：有 Raft 且不是 Leader → 重定向
    if (raft_ && !raft_->isLeader()) {
        std::string leaderAddr;
        if (raft_->getLeaderAddr(leaderAddr)) {
            resp.set_error_code(dkv::common::NOT_LEADER);
            resp.set_correct_leader_addr(leaderAddr);
            dkv::kv::RpcMessage msg;
            msg.set_type(dkv::kv::RpcMessage::SCAN_RESPONSE);
            *msg.mutable_scan_response() = resp;
            LengthPrefixedCodec::send(conn, msg);
            return;
        }
    }

    auto result = engine_->scan(req.start_key(), req.end_key(), req.limit());
    for (auto& kv : result.kvs) {
        auto* kvpb = resp.add_kvs();
        kvpb->set_key(kv.first);
        kvpb->set_value(kv.second);
    }
    resp.set_has_more(result.has_more);
    resp.set_next_start_key(result.next_key);
    resp.set_error_code(dkv::common::OK);

    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::SCAN_RESPONSE);
    *msg.mutable_scan_response() = resp;
    LengthPrefixedCodec::send(conn, msg);
}
