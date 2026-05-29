#pragma once

#include "router.h"
#include "kv_rpc.pb.h"
#include "pd_rpc.pb.h"
#include <string>
#include <vector>
#include <cstdint>

// KV 客户端：封装了路由查找 + TCP 通信 + Leader 重定向
//
// 两种连接模式：
//   直连模式（当前使用）：connect("127.0.0.1", 9100) → 所有请求发往固定地址
//   PD 模式（未启用）：  connectToPd("127.0.0.1", 8100) → 从 PD 拉路由表，按 key 分片路由
//
// 关键行为：
//   - 每次请求新建 TCP 连接（短连接），发完就关，不保持长连接
//   - put/get/del/scan 遇到 NOT_LEADER 时自动重定向到正确 Leader（最多 3 次）
//   - 不依赖 muduo，只用原生 POSIX socket
class KvClient {
public:
    KvClient();
    ~KvClient();  // 自动关闭到 PD 的连接（如果有的话）

    // ---- 连接 ----

    // 直连模式：记下目标地址（不做 TCP 连接），后续所有请求发到这个地址
    bool connect(const std::string& ip, uint16_t port);
    // 关闭到 PD 的长连接（直连模式下是空操作）
    void close();

    // PD 模式（未启用）：连接 PD → 拉 ShardMap → 存入 Router
    bool connectToPd(const std::string& pdIp, uint16_t pdPort);

    // ---- KV 操作（自动路由 + 重定向重试） ----

    // 写入一个 KV 对（覆盖语义：key 存在则更新，不存在则新建）
    bool put(const std::string& key, const std::string& value);
    // 读取 key 对应的 value，成功返回 true 并写入 value 参数，key 不存在返回 false
    bool get(const std::string& key, std::string& value);
    // 删除 key，key 不存在返回 false
    bool del(const std::string& key);
    // 范围扫描 [start_key, end_key)，最多返回 limit 条
    std::vector<std::pair<std::string, std::string>>
        scan(const std::string& start_key,
             const std::string& end_key,
             uint32_t limit = 100);

    // ---- 路由管理 ----

    // 注入自定义 Router（默认使用内部的 defaultRouter_）
    void setRouter(Router* router);

private:
    // 把 req 发给 addr，收 resp。遇到 NOT_LEADER 自动递归重试（最多 maxRetries 次）
    bool sendToAddr(const std::string& addr,
                    const dkv::kv::RpcMessage& req,
                    dkv::kv::RpcMessage& resp,
                    int maxRetries = 3);

    // 最底层的 TCP 通信：连到 ip:port → 发长度前缀 + data → 收响应 → 关连接
    bool sendRaw(const std::string& ip, uint16_t port,
                 const std::string& data, std::string& respData);

    // 从 PD 拉 ShardMap 存入 Router（PD 模式专用，当前未使用）
    bool fetchShardMap();

    // ==================== 成员变量 ====================

    // 到 PD 的长连接 socket，直连模式始终为 -1
    int pdSock_ = -1;

    // 内置路由器对象（存储目标地址或分片路由表），connect() / connectToPd() 往里写
    Router defaultRouter_;

    // 指向当前使用的路由器，默认指向 &defaultRouter_，可通过 setRouter() 覆盖
    Router* router_ = nullptr;

    // 是否使用 PD 模式（当前永远为 false，因为 demo_cli 只调 connect 不调 connectToPd）
    bool usePd_ = false;
};
