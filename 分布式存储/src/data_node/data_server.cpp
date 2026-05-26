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
    while (buf->readableBytes() >= 4) {
        uint32_t len = 0;
        memcpy(&len, buf->peek(), 4);
        len = ntohl(len);
        if (buf->readableBytes() < 4 + len) break;

        buf->retrieve(4);
        std::string payload = buf->retrieveAsString(len);

        // API 端口只处理客户端 RpcMessage
        dkv::kv::RpcMessage msg;
        if (msg.ParseFromString(payload)) {
            dispatchClientRpc(conn, msg);
        } else {
            std::cerr << "Failed to parse RpcMessage" << std::endl;
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
