# 项目文件关系图

## 阅读说明

```
A ──使用──→ B   表示 A 的代码中 #include B 或调用了 B 的接口
    原因: 为什么 A 需要 B
```

---

## 一、proto 文件（4 个）—— 协议定义层

所有消息格式的定义。编译后生成 `.pb.h/.pb.cc` 供 C++ 代码使用。

```
common.proto ──被 import──→ kv_rpc.proto    原因: 需要 ErrorCode 类型
common.proto ──被 import──→ pd_rpc.proto    原因: 需要 ErrorCode、NodeInfo、ShardMap 类型
raft_rpc.proto                    独立，不依赖其他 proto
```

---

## 二、src/common/（3 对文件）—— 公共工具层

### codec.h / codec.cpp

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **kv_service.cpp** | 调用 `LengthPrefixedCodec::send()` | 回复客户端 Response 时，需要把 Protobuf 消息加长度前缀后发送 |
| **test_codec.cpp** | 测试 codec 逻辑 | 验证长度前缀编解码正确性 |

```
codec.h/cpp ←── kv_service.cpp    [回复客户端时加长度前缀]
codec.h/cpp ←── test_codec.cpp    [测试编解码]
```

### config.h / config.cpp

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **data_node/main.cpp** | `#include "config.h"` | 从配置文件读取 IP、端口、db 路径 |
| **kv_client.cpp** | `#include "config.h"` | 客户端 SDK 中编译依赖（实际未充分使用） |

```
config.h/cpp ←── main.cpp         [读取服务器配置]
config.h/cpp ←── kv_client.cpp    [客户端配置读取]
```

### shard_router.h / shard_router.cpp

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **test_shard_router.cpp** | 测试 shard 路由逻辑 | 验证 `getShardId("apple")` → shard 1 的分片正确性 |

```
shard_router.h/cpp ←── test_shard_router.cpp  [测试分片路由]
```

> 注：客户端 SDK 中另有 `router.h/cpp`，功能类似但面向客户端场景（含 PD 模式）。shard_router 是为数据节点侧设计的分片查找。

---

## 三、src/data_node/（5 对文件 + main.cpp）—— 数据节点层

### main.cpp

入口文件，创建 EventLoop → 创建 DataServer → 启动。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **data_server.h** | 创建 `DataServer` 对象 | 这是服务器的核心类 |
| **config.h** | 解析命令行参数 | 读 IP、端口、节点 ID、peer 列表 |
| **muduo** | `EventLoop`、`InetAddress` | 事件循环 + 绑定地址 |

```
main.cpp ──创建──→ DataServer
main.cpp ──使用──→ muduo (EventLoop, InetAddress)
main.cpp ──使用──→ config.h
```

### data_server.h / data_server.cpp

服务器的外壳：创建 TcpServer → 监听端口 → 收到消息分发给 KvService。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **RocksEngine** | 创建 `shared_ptr<RocksEngine>` | 提供持久化存储，传给 KvService 和 RaftService |
| **KvService** | 创建并持有 | 处理客户端 GET/PUT/DELETE/SCAN 请求 |
| **RaftService** | 创建并持有（集群模式） | 提供 Raft 共识，写操作需要走 Raft |
| **muduo** | `TcpServer`、`Buffer` | 监听 API 端口、接收字节流、解析长度前缀帧 |

```
data_server.cpp ──创建──→ RocksEngine    [唯一创建点，通过 shared_ptr 共享]
data_server.cpp ──创建──→ KvService      [注入 engine_ + raft_]
data_server.cpp ──创建──→ RaftService    [注入 engine_ + peer 信息]
data_server.cpp ──使用──→ muduo TcpServer [监听 :9100]
data_server.cpp ──使用──→ kv_rpc.pb.h     [解析 RpcMessage]
```

### kv_service.h / kv_service.cpp

处理客户端 KV 请求的业务逻辑。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **RocksEngine** | 持有 `shared_ptr<RocksEngine>` | 读操作直接查 RocksDB，写操作在单节点模式直接写 |
| **RaftService** | 持有 `RaftService*` 指针 | 集群模式下写操作走 `raft_->propose()` |
| **codec.h** | 调用 `LengthPrefixedCodec::send()` | 回复响应时需要加长度前缀 |
| **raft_service.h** | `#include` 获取 `RaftService` 类型 | 判断 `raft_->isLeader()` 决定处理逻辑 |
| **muduo** | `TcpConnectionPtr` | 通过 conn 发回响应 |
| **kv_rpc.pb.h** | 使用 PutRequest/GetResponse 等类型 | 解析请求、构造响应 |

```
kv_service.cpp ──读──→ RocksEngine      [get/scan]
kv_service.cpp ──写──→ RaftService       [propose 写操作]
kv_service.cpp ──写──→ RocksEngine       [单节点模式直接 put/del]
kv_service.cpp ──发──→ codec.h           [加长度前缀发送响应]
kv_service.cpp ──用──→ kv_rpc.pb.h       [Protobuf 消息类型]
```

### rocks_engine.h / rocks_engine.cpp

RocksDB 的薄封装层。不依赖项目其他文件。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **rocksdb 库** | `rocksdb::DB`、`rocksdb::Options` 等 | 提供 LSM-Tree 持久化 KV 存储 |

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **data_server.cpp** | 创建实例 | 唯一创建点，通过 shared_ptr 共享 |
| **kv_service.cpp** | 调用 put/get/del/scan | 处理客户端请求 |
| **raft_service.cpp** | 调用 put/del | Raft commit 后真正执行写入；持久化 Raft 状态 |
| **test_single_node.cpp** | 直接创建实例测试 | 8 个单元测试 |

```
RocksDB 库 ←── rocks_engine.cpp    [封装 RocksDB 原生 API]
rocks_engine ──被持有──→ kv_service.cpp      [读写删扫]
rocks_engine ──被持有──→ raft_service.cpp     [apply + 持久化]
rocks_engine ──被创建──→ data_server.cpp      [唯一创建点]
rocks_engine ──被测试──→ test_single_node.cpp [8 个测试]
```

### raft_node.h / raft_node.cpp

Raft 共识核心算法：Leader 选举 + 日志复制 + commit 推进。**不依赖任何外部网络或存储**，纯算法实现。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **raft_rpc.pb.h** | 使用 `RequestVoteRequest`、`AppendEntriesRequest`、`LogEntry` 等 | Raft RPC 消息类型 |

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **raft_service.cpp** | 创建并持有 `unique_ptr<RaftNode>` | 注入回调，驱动 tick，收 RPC 时分发到 handleXXX |

```
raft_node ──使用──→ raft_rpc.pb.h       [RequestVote/AppendEntries 消息类型]
raft_node ──被持有──→ raft_service.cpp   [唯一调用者]
```

### raft_service.h / raft_service.cpp

Raft 的网络层：把 RaftNode 和 TCP 网络连起来。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **RaftNode** | 创建并持有 | 核心共识逻辑 |
| **RocksEngine** | 持有 `shared_ptr<RocksEngine>` | onApply 回调中写 RocksDB；persistRaftState 持久化 Raft 状态 |
| **muduo TcpClient** | 创建 N 个客户端 | 连到每个 peer 节点，发送 Raft RPC |
| **muduo Buffer** | 解析长度前缀帧 | 收到 peer 消息后拆包 |
| **raft_rpc.pb.h** | 解析/构造 RaftMessage | 收发电举/复制/心跳消息 |

| 谁用它 | 关系 | 原因 |
|--------|------|------|
| **data_server.cpp** | 创建并持有 | 集群模式下创建，提供 Raft 能力 |
| **kv_service.cpp** | 持有指针 | 判断 `isLeader()` + 调用 `propose()` |

```
raft_service ──持有──→ RaftNode          [核心算法]
raft_service ──持有──→ RocksEngine       [apply 写入 + 持久化状态]
raft_service ──持有──→ TcpClient × N     [连 peer]
raft_service ──使用──→ raft_rpc.pb.h     [Raft RPC 消息]
raft_service ──使用──→ muduo             [网络 I/O]

raft_service ←──创建── data_server.cpp   [集群模式]
raft_service ←──使用── kv_service.cpp    [isLeader + propose]
```

---

## 四、src/pd_server/（3 对文件 + main.cpp）—— PD 元数据服务器层

### main.cpp

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **pd_server.h** | 创建 `PdServer` | 启动 PD 服务 |
| **muduo** | `EventLoop`、`InetAddress` | 事件循环 |

### pd_server.h / pd_server.cpp

PD 的 TcpServer 外壳。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **ShardManager** | 创建并持有 | 管理集群元数据 |
| **PdService** | 创建并持有，注入 ShardManager | 处理 RPC 业务逻辑 |
| **muduo TcpServer** | 创建 TcpServer | 监听 :8100 等待数据节点/客户端连入 |

### pd_service.h / pd_service.cpp

PD RPC 的处理逻辑。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **ShardManager** | 持有指针 | 调 `registerNode`/`getShardMap`/`processHeartbeat` |
| **pd_rpc.pb.h** | 解析/构造 PD 消息 | RegisterNode/Heartbeat/GetShardMap |
| **muduo TcpConnection** | 通过 conn 发回响应 | 回复数据节点 |

### shard_manager.h / shard_manager.cpp

PD 核心元数据：节点注册、心跳、shard 分配、rebalance。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **common.pb.h** | 使用 `NodeInfo`、`ShardMeta`、`ShardMap` | 元数据结构 |
| **pd_rpc.pb.h** | 使用 `HeartbeatRequest::ShardState` | 心跳信息 |

```
pd_server ──持有──→ ShardManager         [元数据存储]
pd_server ──持有──→ PdService            [RPC 处理]
PdService  ──调用──→ ShardManager         [节点注册、心跳、路由查询]
PdService  ──使用──→ pd_rpc.pb.h          [PD 消息类型]
```

---

## 五、src/client_sdk/（3 对文件 + demo_cli.cpp）—— 客户端层

### kv_client.h / kv_client.cpp

客户端 API：put/get/del/scan。处理路由、连接、重定向。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **Router** | 持有 `Router*` | 根据 key 查目标节点地址 |
| **kv_rpc.pb.h** | 构造/解析 RpcMessage | 与数据节点通信 |
| **pd_rpc.pb.h** | 解析 GetShardMapResponse | 从 PD 拉路由表 |
| **socket API** | `socket()/connect()/send()/recv()` | 原生 TCP 通信（不用 muduo） |

### router.h / router.cpp

客户端路由缓存。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **common.pb.h** | 存储 `ShardMap` | 缓存分片路由表 |

### demo_cli.cpp

命令行工具。

| 它用谁 | 关系 | 原因 |
|--------|------|------|
| **KvClient** | 创建实例，调 put/get/del/scan | 提供交互式命令行 |

```
demo_cli ──调用──→ KvClient              [命令行 → KV 操作]
KvClient ──持有──→ Router                [key → 目标地址]
KvClient ──使用──→ kv_rpc.pb.h           [构造 RpcMessage]
KvClient ──使用──→ socket API            [TCP 通信]
Router   ──使用──→ common.pb.h           [存储 ShardMap]
```

---

## 六、tests/（3 个测试文件）

### test_codec.cpp

| 它测谁 | 原因 |
|--------|------|
| **codec.h/cpp** | 验证长度前缀编解码：构造帧 → 模拟接收 → 校验解析正确 |

### test_shard_router.cpp

| 它测谁 | 原因 |
|--------|------|
| **shard_router.h/cpp** | 验证 Range 分片：输入 key 能正确找到对应 shard |

### test_single_node.cpp

| 它测谁 | 原因 |
|--------|------|
| **RocksEngine** | 直接测 RocksDB 的 put/get/del/scan/batch，8 个 case，不经过网络 |

---

## 七、完整依赖图（按数据流向）

```
                    ┌─────────────┐
                    │  proto/*.proto │  ← 所有消息格式定义
                    └──────┬──────┘
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
    kv_rpc.pb.h     raft_rpc.pb.h    pd_rpc.pb.h
           │               │               │
           ▼               ▼               ▼
    ┌──────────┐   ┌──────────┐   ┌──────────────┐
    │ KvService│   │ RaftNode │   │ PdService    │
    │ 处理客户端│   │ 选举+复制│   │ 节点注册+心跳│
    │ KV 请求  │   │ +commit  │   │ +路由查询    │
    └────┬─────┘   └────┬─────┘   └──────┬───────┘
         │              │                │
         │         ┌────▼────┐           │
         │         │RaftService│          │
         │         │网络+RPC  │          │
         │         └────┬────┘           │
         │              │                │
         └──────┬───────┘                │
                ▼                        ▼
         ┌─────────────┐         ┌──────────────┐
         │ RocksEngine  │         │ ShardManager  │
         │ put/get/del  │         │ 节点+分片管理 │
         │ /scan/batch  │         └──────────────┘
         └──────┬───────┘
                ▼
         ┌─────────────┐
         │   RocksDB   │  ← 外部库（Facebook）
         │  LSM-Tree   │
         │  磁盘存储    │
         └─────────────┘

         ┌─────────────┐
         │    muduo    │  ← 外部库（网络 I/O）
         │  epoll+TCP  │
         └─────────────┘
         被以下文件使用:
         data_server.cpp, raft_service.cpp, pd_server.cpp,
         kv_service.cpp, pd_service.cpp, codec.cpp, main.cpp×2
```

---

## 八、按"被依赖次数"排序

文件被越多地方使用，说明它越基础越重要。

| 排名 | 文件 | 被依赖次数 | 谁在用 |
|------|------|-----------|--------|
| 1 | **rocks_engine.h/cpp** | 4 | data_server, kv_service, raft_service, test_single_node |
| 2 | **kv_service.h/cpp** | 2 | data_server（创建）, data_server（调用） |
| 2 | **raft_service.h/cpp** | 2 | data_server（创建）, kv_service（调 isLeader/propose） |
| 2 | **codec.h/cpp** | 2 | kv_service（发响应）, test_codec（测试） |
| 2 | **raft_node.h/cpp** | 1+间接 | raft_service（唯一直接调用）, kv_service（通过 raft_service） |
| 3 | **data_server.h/cpp** | 1 | main.cpp（创建） |
| 3 | **pd_server.h/cpp** | 1 | main.cpp（创建） |
| 3 | **shard_manager.h/cpp** | 1 | pd_service（调用） |
| 3 | **kv_client.h/cpp** | 1 | demo_cli（调用） |
| 3 | **router.h/cpp** | 1 | kv_client（调用） |
| 4 | **config.h/cpp** | 1 | main.cpp |
| 4 | **shard_router.h/cpp** | 0 | 仅被测试使用（客户端另有自己的 router） |
