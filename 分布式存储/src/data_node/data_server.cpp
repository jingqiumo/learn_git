#include "data_server.h"
#include "codec.h"
#include "kv_rpc.pb.h"
#include "raft_rpc.pb.h"
#include <muduo/base/Timestamp.h>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

using namespace muduo::net;
using muduo::Timestamp;

// ==================== Phase 1 构造（单节点，无 Raft） ====================
DataServer::DataServer(EventLoop* loop,
                       const InetAddress& addr,
                       const std::string& name,
                       const std::string& db_path)
    : server_(loop, addr, name)
    , engine_(std::make_shared<RocksEngine>(db_path))
    , useRaft_(false)
{
    service_ = std::make_unique<KvService>(engine_, nullptr);
    server_.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) { onConnection(conn); });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp ts) {
            onMessage(conn, buf, ts);
        });
}

// ==================== Phase 2 构造（Raft 集群） ====================
DataServer::DataServer(EventLoop* loop,
                       const InetAddress& addr,
                       uint64_t nodeId,
                       const std::unordered_map<uint64_t, std::string>& peers,
                       const std::string& db_path)
    : server_(loop, addr, "dkv-api")
    , engine_(std::make_shared<RocksEngine>(db_path))
    , useRaft_(true)
    , nodeId_(nodeId)
{
    raft_ = std::make_unique<RaftService>(loop, nodeId, peers, engine_);
    service_ = std::make_unique<KvService>(engine_, raft_.get());

    server_.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) { onConnection(conn); });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp ts) {
            onMessage(conn, buf, ts);
        });
}

void DataServer::start() {
    if (!engine_->open()) {
        std::cerr << "Failed to open RocksDB" << std::endl;
        return;
    }
    std::cout << "RocksDB opened" << std::endl;

    if (raft_) {
        raft_->start();
    }

    server_.start();
    std::cout << "DataServer started on " << server_.ipPort() << std::endl;
}

void DataServer::tick() {
    if (raft_) raft_->tick();
}

void DataServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        std::cout << "Client connected: " << conn->peerAddress().toIpPort()
                  << std::endl;
    } else {
        std::cout << "Client disconnected: " << conn->peerAddress().toIpPort()
                  << std::endl;
    }
}

void DataServer::onMessage(const TcpConnectionPtr& conn,
                            Buffer* buf, Timestamp ts) {
    while (buf->readableBytes() >= 5) {  // 4字节长度 + 1字节类型
        uint32_t len = 0;
        memcpy(&len, buf->peek(), 4);
        len = ntohl(len);
        if (buf->readableBytes() < 5 + len) break;   // 4字节头 + 1字节类型 + 数据

        buf->retrieve(4);
        char msgType = *(buf->peek());
        buf->retrieve(1);
        std::string payload = buf->retrieveAsString(len);

        if (msgType == 0x01 && raft_) {
            // Raft 消息
            dkv::raft::RaftMessage raftMsg;
            if (raftMsg.ParseFromString(payload)) {
                uint64_t fromNode = 0;
                if (raftMsg.type() == dkv::raft::RaftMessage::REQUEST_VOTE)
                    fromNode = raftMsg.request_vote().candidate_id();
                else if (raftMsg.type() == dkv::raft::RaftMessage::APPEND_ENTRIES)
                    fromNode = raftMsg.append_entries().leader_id();
                raft_->handleRaftMessage(conn, raftMsg, fromNode);
            }
        } else {
            // 客户端消息
            dkv::kv::RpcMessage rpcMsg;
            if (rpcMsg.ParseFromString(payload)) {
                dispatchClientRpc(conn, rpcMsg);
            }
        }
    }
}

void DataServer::dispatchClientRpc(const TcpConnectionPtr& conn,
                                    const dkv::kv::RpcMessage& msg) {
    switch (msg.type()) {
    case dkv::kv::RpcMessage::GET_REQUEST:
        service_->handleGet(conn, msg.get_request());
        break;
    case dkv::kv::RpcMessage::PUT_REQUEST:
        service_->handlePut(conn, msg.put_request());
        break;
    case dkv::kv::RpcMessage::DELETE_REQUEST:
        service_->handleDelete(conn, msg.delete_request());
        break;
    case dkv::kv::RpcMessage::SCAN_REQUEST:
        service_->handleScan(conn, msg.scan_request());
        break;
    default:
        break;
    }
}
