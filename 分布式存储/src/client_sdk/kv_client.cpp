#include "kv_client.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

KvClient::KvClient() : router_(nullptr) {}

KvClient::~KvClient() { close(); }

bool KvClient::connect(const std::string& ip, uint16_t port) {
    defaultRouter_.setDirectNode(ip + ":" + std::to_string(port));
    router_ = &defaultRouter_;
    return true;
}

void KvClient::close() {
    if (pdSock_ >= 0) {
        ::close(pdSock_);
        pdSock_ = -1;
    }
}

bool KvClient::connectToPd(const std::string& pdIp, uint16_t pdPort) {
    pdSock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (pdSock_ < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(pdPort);
    if (::inet_pton(AF_INET, pdIp.c_str(), &addr.sin_addr) <= 0) return false;
    if (::connect(pdSock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) return false;

    defaultRouter_.setPdAddr(pdIp + ":" + std::to_string(pdPort));
    router_ = &defaultRouter_;
    usePd_ = true;

    return fetchShardMap();
}

bool KvClient::fetchShardMap() {
    dkv::pd::GetShardMapRequest req;
    std::string data;
    req.SerializeToString(&data);

    std::string respData;
    uint32_t len = htonl(data.size());
    if (::send(pdSock_, &len, 4, 0) != 4) return false;
    if (::send(pdSock_, data.data(), data.size(), 0) != (ssize_t)data.size()) return false;

    uint32_t respLen = 0;
    if (::recv(pdSock_, &respLen, 4, MSG_WAITALL) != 4) return false;
    respLen = ntohl(respLen);
    respData.resize(respLen);
    if (::recv(pdSock_, respData.data(), respLen, MSG_WAITALL) != (ssize_t)respLen) return false;

    dkv::pd::GetShardMapResponse resp;
    if (!resp.ParseFromString(respData)) return false;

    router_->loadShardMap(resp.shard_map());
    // 填充 node address 映射
    for (int i = 0; i < resp.shard_map().shards_size(); ++i) {
        // PD 应返回带地址的 shard map，此处为简化版
    }

    std::cout << "[Client] Loaded shard map, " << resp.shard_map().shards_size()
              << " shards" << std::endl;
    return true;
}

bool KvClient::sendRaw(const std::string& ip, uint16_t port,
                        const std::string& data, std::string& respData) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        ::close(sock);
        return false;
    }
    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(sock);
        return false;
    }

    uint32_t len = htonl(data.size());
    bool ok = (::send(sock, &len, 4, 0) == 4)
           && (::send(sock, data.data(), data.size(), 0) == (ssize_t)data.size());

    if (ok) {
        uint32_t rlen = 0;
        if (::recv(sock, &rlen, 4, MSG_WAITALL) == 4) {
            rlen = ntohl(rlen);
            respData.resize(rlen);
            if (::recv(sock, respData.data(), rlen, MSG_WAITALL) != (ssize_t)rlen) ok = false;
        } else ok = false;
    }

    ::close(sock);
    return ok;
}

bool KvClient::sendToAddr(const std::string& addr,
                           const dkv::kv::RpcMessage& req,
                           dkv::kv::RpcMessage& resp,
                           int maxRetries) {
    for (int retry = 0; retry < maxRetries; ++retry) {
        size_t colon = addr.find(':');
        if (colon == std::string::npos) return false;
        std::string ip = addr.substr(0, colon);
        uint16_t port = static_cast<uint16_t>(std::stoi(addr.substr(colon + 1)));

        std::string reqData, respData;
        req.SerializeToString(&reqData);
        if (!sendRaw(ip, port, reqData, respData)) return false;
        if (!resp.ParseFromString(respData)) return false;

        // 检查是否需要重定向
        if (resp.type() == dkv::kv::RpcMessage::PUT_RESPONSE
            && resp.put_response().error_code() == dkv::common::NOT_LEADER) {
            // 更新路由并重试
            std::string newAddr = resp.put_response().correct_leader_addr();
            if (!newAddr.empty()) {
                std::cout << "[Client] Redirected to " << newAddr << std::endl;
                return sendToAddr(newAddr, req, resp, maxRetries - 1);
            }
        }
        if (resp.type() == dkv::kv::RpcMessage::GET_RESPONSE
            && resp.get_response().error_code() == dkv::common::NOT_LEADER) {
            std::string newAddr = resp.get_response().correct_leader_addr();
            if (!newAddr.empty()) {
                std::cout << "[Client] Redirected to " << newAddr << std::endl;
                return sendToAddr(newAddr, req, resp, maxRetries - 1);
            }
        }
        return true;
    }
    return false;
}

// ==================== KV API ====================

bool KvClient::put(const std::string& key, const std::string& value) {
    if (!router_) return false;

    dkv::kv::RpcMessage req;
    req.set_type(dkv::kv::RpcMessage::PUT_REQUEST);
    req.mutable_put_request()->set_key(key);
    req.mutable_put_request()->set_value(value);
    req.mutable_put_request()->set_call_id(1);

    std::string addr = router_->getAddrForKey(key);
    if (addr.empty()) return false;

    dkv::kv::RpcMessage resp;
    if (!sendToAddr(addr, req, resp)) return false;
    return resp.put_response().error_code() == dkv::common::OK;
}

bool KvClient::get(const std::string& key, std::string& value) {
    if (!router_) return false;

    dkv::kv::RpcMessage req;
    req.set_type(dkv::kv::RpcMessage::GET_REQUEST);
    req.mutable_get_request()->set_key(key);
    req.mutable_get_request()->set_call_id(1);

    std::string addr = router_->getAddrForKey(key);
    if (addr.empty()) return false;

    dkv::kv::RpcMessage resp;
    if (!sendToAddr(addr, req, resp)) return false;
    if (resp.get_response().error_code() == dkv::common::OK) {
        value = resp.get_response().value();
        return true;
    }
    return false;
}

bool KvClient::del(const std::string& key) {
    if (!router_) return false;

    dkv::kv::RpcMessage req;
    req.set_type(dkv::kv::RpcMessage::DELETE_REQUEST);
    req.mutable_delete_request()->set_key(key);
    req.mutable_delete_request()->set_call_id(1);

    std::string addr = router_->getAddrForKey(key);
    if (addr.empty()) return false;

    dkv::kv::RpcMessage resp;
    if (!sendToAddr(addr, req, resp)) return false;
    return resp.delete_response().error_code() == dkv::common::OK;
}

std::vector<std::pair<std::string, std::string>>
KvClient::scan(const std::string& start_key,
               const std::string& end_key,
               uint32_t limit) {
    std::vector<std::pair<std::string, std::string>> result;
    if (!router_) return result;

    dkv::kv::RpcMessage req;
    req.set_type(dkv::kv::RpcMessage::SCAN_REQUEST);
    req.mutable_scan_request()->set_start_key(start_key);
    req.mutable_scan_request()->set_end_key(end_key);
    req.mutable_scan_request()->set_limit(limit);
    req.mutable_scan_request()->set_call_id(1);

    std::string addr = router_->getAddrForKey(start_key);
    if (addr.empty()) return result;

    dkv::kv::RpcMessage resp;
    if (!sendToAddr(addr, req, resp)) return result;
    for (auto& kv : resp.scan_response().kvs()) {
        result.emplace_back(kv.key(), kv.value());
    }
    return result;
}

void KvClient::setRouter(Router* router) {
    router_ = router;
}
