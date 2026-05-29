# 分布式 KV 存储系统（DKV）— 学习指南

> C++17 / muduo / Raft / RocksDB / Protobuf / CMake
> 42 个源文件 | 3238 行代码 | 14 个测试用例 | 7 个构建目标

---

## 一、项目概述

一个分布式 KV 数据库。你 `put("name", "张三")`，数据经过 Raft 共识复制到 3 台机器，然后写入 RocksDB。任何一台机器宕机，集群正常工作。

### 架构图

```
                         +-----------+
                         | PD Server |  (元数据：shard 路由、节点心跳)
                         +-----+-----+
                               |
              心跳 / 注册 / GetShardMap
           +-------------------+-------------------+
           |                   |                   |
    +------v------+    +------v------+    +------v------+
    |  Data Node 1 |    |  Data Node 2 |    |  Data Node 3 |
    |  (Raft 副本)  |◄──►|  (Raft 副本)  |◄──►|  (Raft 副本)  |
    |  + RocksDB   |    |  + RocksDB   |    |  + RocksDB   |
    +--------------+    +--------------+    +--------------+
           ^                   ^                   ^
           +-------------------+-------------------+
                               |
                         +-----+-----+
                         | Client SDK |  (路由缓存、自动重定向)
                         +-----------+
```

### 技术栈

| 层级 | 技术 | 在项目里的作用 |
|------|------|---------------|
| 序列化 | Protobuf | 所有消息的编解码格式 |
| 网络库 | muduo (Reactor + epoll) | TcpServer/TcpClient/Buffer/EventLoop |
| 存储引擎 | RocksDB (LSM-Tree) | 单机持久化 KV 存储 |
| 共识算法 | Raft（手写） | 多节点数据一致性 |
| 分布式 | 分片/副本/Leader 选举/心跳/重定向 | 集群协作机制 |
| C++ | 智能指针/RAII/lambda/mutex | 代码组织方式 |

---

## 二、目录结构

```
分布式存储/
├── CMakeLists.txt                    # 7 个构建目标
├── proto/
│   ├── common.proto                  # ErrorCode, ShardMeta, ShardMap
│   ├── kv_rpc.proto                  # Get/Put/Delete/Scan 的请求/响应
│   ├── raft_rpc.proto                # Raft RPC: RequestVote, AppendEntries
│   └── pd_rpc.proto                  # PD RPC: RegisterNode, Heartbeat, GetShardMap
├── src/
│   ├── common/                       # 公共库
│   │   ├── codec.h/cpp              #    长度前缀编解码（4字节长度+Protobuf数据）
│   │   ├── config.h/cpp             #    配置文件解析（key=value）
│   │   └── shard_router.h/cpp       #    Range-based 分片路由
│   ├── data_node/                    # 数据节点
│   │   ├── main.cpp                 #    入口
│   │   ├── data_server.h/cpp        #    muduo TcpServer，分发客户端请求
│   │   ├── kv_service.h/cpp         #    处理 GET/PUT/DELETE/SCAN
│   │   ├── rocks_engine.h/cpp       #    RocksDB 封装
│   │   ├── raft_node.h/cpp          #    Raft 共识核心（选举/复制/commit）
│   │   └── raft_service.h/cpp       #    Raft 网络层（节点互联、RPC收发）
│   ├── pd_server/                    # PD 元数据服务器
│   │   ├── main.cpp                 #    入口
│   │   ├── pd_server.h/cpp          #    muduo TcpServer
│   │   ├── pd_service.h/cpp         #    PD RPC 业务处理
│   │   └── shard_manager.h/cpp      #    节点注册/心跳/shard调度
│   └── client_sdk/                   # 客户端
│       ├── kv_client.h/cpp          #    同步客户端（直连/PD模式、重定向）
│       ├── router.h/cpp             #    路由缓存
│       └── demo_cli.cpp             #    命令行交互工具
├── tests/
│   ├── unit/
│   │   ├── test_codec.cpp           #    编解码器测试 (3 cases)
│   │   └── test_shard_router.cpp    #    分片路由测试 (3 cases)
│   └── integration/
│       └── test_single_node.cpp     #    RocksDB 集成测试 (8 cases)
├── configs/                          # 节点配置文件
├── scripts/                          # 启动/停止脚本
└── bin/                              # 编译产物
    ├── pd_server                     # PD 元数据服务器
    ├── data_node                     # 数据节点
    ├── dkv_cli                       # 命令行客户端
    ├── test_codec                    # 编解码测试
    ├── test_shard_router             # 路由测试
    └── test_single_node              # 存储测试
```

### 代码量统计

| 模块 | 行数 | 做了什么 |
|------|------|----------|
| Raft 共识 | 904 | 选举/复制/commit、节点间 RPC 组网 |
| 协议定义 | 243 | 所有通信格式 |
| 客户端 | 493 | 路由、发送、重定向、命令行交互 |
| 数据节点 | 499 | 接收请求、分发、读写 RocksDB |
| PD 服务器 | 380 | 节点注册、心跳、shard 调度 |
| 公共库 | 246 | 编解码、配置文件、分片路由 |
| 测试 | 241 | 编解码/路由/存储测试 |
| 构建+配置 | 232 | 构建系统、启动脚本 |
| **总计** | **3238** | |

---

## 三、分层详解

### 第一层：协议定义（proto/）

这是整个系统的"合同"——所有通信的格式。

#### proto/common.proto

定义到处复用的基础类型：

- `ErrorCode`：错误码枚举
  - `OK = 0` 正常
  - `KEY_NOT_FOUND = 1` 键不存在
  - `NOT_LEADER = 5` 本节点不是 Leader，请重定向
- `ShardMeta`：描述一个分片的元数据（shard_id、key 范围 `[start_key, end_key)`、leader 是谁、副本在哪些节点上）
- `ShardMap`：所有分片的集合。客户端拉取后用来做路由：收到 `put("foo", v)`，遍历 shards，找到 `start_key <= "foo" < end_key` 的那个 shard，发到它的 leader

#### proto/kv_rpc.proto

定义客户端和数据节点之间的通信格式：

```
RpcMessage {
    type = PUT_REQUEST | PUT_RESPONSE | GET_REQUEST | GET_RESPONSE
         | DELETE_REQUEST | DELETE_RESPONSE | SCAN_REQUEST | SCAN_RESPONSE
    根据 type 取对应字段: get_request / put_request / delete_request / scan_request
}
```

**使用示例**：

```
客户端 → 服务器:
  RpcMessage{type=PUT_REQUEST, put_request={key="name", value="张三"}}

服务器 → 客户端:
  RpcMessage{type=PUT_RESPONSE, put_response={error_code=OK}}
```

#### proto/raft_rpc.proto

定义 Raft 节点之间的通信（内部使用，客户端不感知）：

- `RequestVoteRequest/Response`：候选者请求投票，"选我当 Leader"
- `AppendEntriesRequest/Response`：Leader 把日志复制给 Follower（也复用作心跳包）
- `LogEntry`：一条日志 = term + index + 序列化的 KV 命令

#### proto/pd_rpc.proto

定义数据节点和 PD 之间的通信：

- `RegisterNodeRequest`：数据节点启动时向 PD 报到
- `HeartbeatRequest`：数据节点定期上报自身状态
- `GetShardMapRequest`：客户端/节点向 PD 查询最新路由表

---

### 第二层：公共库（src/common/）

#### codec.h/cpp —— 网络编解码

TCP 是字节流，发两个包可能粘成一个包。codec 定义分割消息的规则。

**协议**：4 字节大端整数（消息长度） + Protobuf 序列化数据

```
[00 00 00 0A][Hello World]     ← 10 字节的消息
  ↑长度=10     ↑实际数据
```

**接收端解析逻辑**：

```cpp
// 1. 读 4 字节长度
uint32_t len; memcpy(&len, buf->peek(), 4); len = ntohl(len);
// 2. 如果缓冲区不够一个完整帧，return 等待更多数据
if (buf->readableBytes() < 4 + len) return;
// 3. 消费长度前缀，取出消息体
buf->retrieve(4);
string payload = buf->retrieveAsString(len);
// 4. 反序列化
RpcMessage msg; msg.ParseFromString(payload);
```

#### shard_router.h/cpp —— 分片路由

根据 key 查它属于哪个 shard。使用 Range-based 分片——按 key 字典序切分。

**示例**（2 个 shard）：

- Shard 1: `["", "m")` — key 从空到 "m"（不含）
- Shard 2: `["m", "")` — key 从 "m" 到无穷

```
getShardId("apple") → shard 1  （"apple" < "m"）
getShardId("zebra") → shard 2  （"zebra" >= "m"）
```

#### config.h/cpp —— 配置文件解析

解析 `key=value` 格式的配置文件。读 `node1.conf` 里的 `ip=127.0.0.1`。

---

### 第三层：存储引擎

#### rocks_engine.h/cpp —— RocksDB 封装

RocksDB 是 Facebook 出品的高性能嵌入式 KV 存储引擎（LevelDB 升级版，LSM-Tree 结构）。

**类比**：相当于 `std::unordered_map<string, string>`，但数据存在磁盘上，断电不丢，支持范围扫描。

**关键特性**：
- 写操作很快（顺序写 WAL + memtable）
- 数据按 key 有序存储，支持高效范围扫描
- LSM-Tree 结构：memtable → SST 文件多层，后台自动 compaction

**提供的 API**：

```cpp
engine->put("key1", "value1");          // 写入
string val;
bool ok = engine->get("key1", val);     // 读取，返回 false 表示不存在
engine->del("key1");                     // 删除

// 范围扫描 [start_key, end_key)，最多返回 limit 条
auto result = engine->scan("a", "f", 100);
// result.kvs = [{"a_key","a_val"}, {"b_key","b_val"}, ...]
// result.has_more = true 表示还有更多数据
// result.next_key 是下次扫描的起点
```

---

### 第四层：Raft 共识（核心难点）

#### 直观理解

没有 Raft 时：客户端写 Node1，Node1 写 RocksDB，OK。Node1 宕机 → 数据丢失。

有 Raft 时：

1. 客户端只能写 **Leader** 节点
2. Leader 不立刻写 RocksDB，先把操作记到**日志**
3. Leader 把日志复制给 2 个 Follower
4. 等到**多数节点**（3 节点需 2 台）确认收到 → 才 commit → 写入 RocksDB
5. Leader 宕机 → Follower 超时 → 自动**选举**新 Leader

#### raft_node.h/cpp —— Raft 核心实现

##### 三种角色状态转换

```
Follower ──超时没收到心跳──→ Candidate ──获得多数票──→ Leader
    ↑                            │                      │
    └────收到更高 term 的消息────┘                      │
                                    发现更高 term 的消息 ↓
                                    └──────────────────→ Follower
```

##### 核心数据结构（对照 Raft 论文 Figure 2）

| 论文概念 | 代码变量 | 类型 |
|----------|---------|------|
| currentTerm | `currentTerm_` | 持久化 |
| votedFor | `votedFor_` | 持久化 |
| log[] | `log_` (vector<LogEntry>) | 持久化 |
| commitIndex | `commitIndex_` | 易失 |
| lastApplied | `lastApplied_` | 易失 |
| nextIndex[] | `nextIndex_` | Leader 专用 |
| matchIndex[] | `matchIndex_` | Leader 专用 |

##### 关键方法

| 方法 | 谁调用 | 做什么 |
|------|--------|--------|
| `propose(cmd)` | KvService（收到客户端写请求） | Leader 把操作追加到自己的日志末尾 |
| `tick()` | muduo 定时器，每 10ms | 检查选举超时 / 发心跳 |
| `handleRequestVote(req)` | 收到投票请求 | 判断是否投票给对方 |
| `handleAppendEntries(req)` | 收到日志复制/心跳 | 把 Leader 日志同步到本地日志 |
| `applyEntries()` | tick() 中自动调用 | 把已 commit 的日志应用到状态机（写 RocksDB） |

##### 写请求完整流程

```
1. Client → KvService: PUT key="name" value="张三"
2. KvService 发现本节点是 Leader → 序列化命令 → raft_->propose(cmd)
3. RaftNode: 追加到 log_[1] = {term=1, index=1, cmd=序列化数据}
4. RaftNode: sendAppendEntries() 发 AppendEntries 给两个 Follower
5. Follower: handleAppendEntries() 把 entry 追加到自己的 log_
6. Follower: 回复 AppendEntriesResponse{success=true}
7. Leader: 收到多数确认 → commitIndex_ = 1
8. Leader: applyEntries() → onApply(1, cmd) → 解析 cmd → RocksDB::put("name","张三")
9. KvService → 回复客户端 "OK"
```

##### 核心概念

- **term（任期）**：每次选举 +1。收到更高 term 的消息自动退位，防止脑裂
- **election timeout**：随机 150~300ms。这段时间没收到 Leader 心跳就开始选举。随机性防止多节点同时参选
- **log matching property**：如果两个日志在同一个 index 上的 term 相同，则之前的所有条目都相同。这是 Raft 安全性的基础
- **committed**：一条日志被多数节点确认后就是 committed，永远不会被覆盖

#### raft_service.h/cpp —— Raft 网络层

把 RaftNode 和网络连接起来：

```
RaftService
├── RaftNode (核心共识逻辑)
│   ├── persistCallback → RaftService::persistRaftState() → RocksDB (存 currentTerm/votedFor)
│   ├── applyCallback   → RaftService::onApply() → RocksDB (执行已提交的KV操作)
│   └── rpcSender       → RaftService::sendRaftRpc() → TCP 发送给 peer
├── TcpClient × N       → 连接到每个 peer 节点
└── 消息接收             → 解析 RaftMessage → 调用 RaftNode::handleXXX()
```

启动时流程：
1. 创建 RaftNode，注入 persist/apply/rpcSender 三个回调
2. 从 RocksDB 恢复上次的 currentTerm/votedFor（recoverRaftState）
3. 用 TcpClient 连接所有 peer
4. 设置收消息回调：收到 RaftMessage → 根据 type 调用对应 handler

---

### 第五层：数据节点服务

#### kv_service.h/cpp —— 请求处理

处理客户端 KV 请求，决定"直接读写 RocksDB"还是"走 Raft"。

```
收到 GET:
  if (有Raft && 我不是Leader) → 回复 NOT_LEADER + leader 地址
  else → engine_->get(key) → 返回结果

收到 PUT:
  if (有Raft && 我不是Leader) → 回复 NOT_LEADER + leader 地址
  else if (有Raft) → raft_->propose(序列化命令) → 返回 OK
  else → engine_->put(key, value) → 返回 OK
```

#### data_server.h/cpp —— 服务器入口

muduo TcpServer 包装，在 onMessage 中：

1. 读 4 字节长度前缀 + Protobuf 数据
2. 解析为 RpcMessage
3. 根据 type 分发给 KvService

支持两种模式：
- **单节点模式**（Phase 1）：`DataServer(loop, addr, name, db_path)` — 不创建 Raft
- **Raft 集群模式**（Phase 2）：`DataServer(loop, addr, nodeId, peers, db_path)` — 创建 RaftService

---

### 第六层：PD 元数据服务器

PD 是集群的"大脑"，维护元数据。

#### shard_manager.h/cpp —— 核心逻辑

**数据结构**：

```cpp
nodes_:  map<nodeId, {ip, port, alive, lastHeartbeat, shardCount}>
shards_: vector<{shardId, startKey, endKey, epoch, leaderNodeId, [peerNodeIds]}>
```

**关键方法**：

| 方法 | 功能 |
|------|------|
| `registerNode(info)` | 数据节点启动时向 PD 报到，分配 nodeId |
| `processHeartbeat(nodeId, state)` | 更新节点的 lastHeartbeat 时间戳 |
| `getDeadNodes()` | 超过 10s 没心跳 → 标记死亡 → 返回死亡列表 |
| `scheduleRebalance()` | 节点挂了 → 把它的 shard 副本迁到活节点 |
| `initShards(n, replica)` | 初始化 n 个分片，按首字母范围切分 |
| `getShardMap()` | 返回全量路由表（客户端/节点使用） |

#### pd_server.h/cpp + pd_service.h/cpp

muduo TcpServer，接收来自数据节点和客户端的请求：

```
收到消息 → 尝试解析为 RegisterNodeRequest → PdService::handleRegisterNode
        → 尝试解析为 HeartbeatRequest     → PdService::handleHeartbeat
        → 尝试解析为 GetShardMapRequest   → PdService::handleGetShardMap
```

---

### 第七层：客户端 SDK

#### router.h/cpp —— 路由缓存

- **直连模式**（Phase 1）：`setDirectNode("127.0.0.1:9100")`，所有请求发这个地址
- **PD 模式**（Phase 3）：从 PD 拉取 ShardMap，`getAddrForKey(key)` 根据 key 找到对应 shard 的 leader 地址

#### kv_client.h/cpp —— 客户端 API

每次请求的流程：

1. Router 查目标地址
2. TCP 连接目标节点，发送 RpcMessage
3. 收到 NOT_LEADER 响应 → 自动重定向到正确 leader → 重试（最多 3 次）
4. 返回结果

**使用示例**：

```cpp
KvClient client;
client.connect("127.0.0.1", 9100);       // 直连模式
// 或 client.connectToPd("127.0.0.1", 8100);  // PD 模式

client.put("name", "张三");              // 写入
string val;
client.get("name", val);                  // 读取 → val = "张三"
client.del("name");                       // 删除

auto kvs = client.scan("a", "z", 10);    // 扫描 [a, z) 最多 10 条
```

#### demo_cli.cpp —— 命令行工具

```bash
./bin/dkv_cli 127.0.0.1 9100
> put name 张三
> get name
> del name
> scan a z
> quit
```

---

## 四、完整数据流（集群模式）

### 启动阶段

```
1. ./pd_server                → PD 监听 :8100，初始化 shard 表
2. ./data_node id=1 peers=... →
   2a. 打开 RocksDB
   2b. 创建 RaftNode(id=1, peers=[2,3])
   2c. RaftService 连到 node2、node3
   2d. DataServer 监听 :9100（客户端 API）
3. 3 个节点都启动 → 选举 → 某节点成为 Leader
4. 各节点向 PD 注册，开始发送心跳
```

### 运行时（一次 PUT）

```
1. dkv_cli → client.connect("127.0.0.1", 9100)
2. client.put("name", "张三")

3. Router 查询 → 目标地址 "127.0.0.1:9100"
4. TCP 连接 → 发送 RpcMessage{type=PUT_REQUEST, put_request={key="name",value="张三"}}

5. Node1 DataServer::onMessage → 解析长度前缀 → 解析 RpcMessage
6. dispatchClientRpc → KvService::handlePut

7. KvService 发现 raft_->isLeader() == false
   → 回复 RpcMessage{type=PUT_RESPONSE, put_response={error_code=NOT_LEADER, correct_leader_addr="127.0.0.1:9200"}}

8. Client 收到 NOT_LEADER → 自动重定向 → 重连 127.0.0.1:9200
9. 重新发送相同的 RpcMessage

10. Node2(Leader) KvService::handlePut → raft_->propose(序列化命令)
11. RaftNode: 追加日志 → sendAppendEntries → 发送给 Node1, Node3
12. Node1, Node3: handleAppendEntries → 追加日志 → 回复 success
13. Node2: 多数确认 → commitIndex++ → applyEntries()
14. onApply → 解析命令 → RocksDB::put("name", "张三")
15. Node2 → 回复 RpcMessage{type=PUT_RESPONSE, put_response={error_code=OK}}

16. Client 收到 OK → 返回给用户
```

---

## 五、运行指南

### 编译

```bash
cd 分布式存储
mkdir -p build && cd build
cmake ..
cmake --build .
# 产物在 ../bin/ 下
```

### 单节点模式验证

```bash
# 终端 1：启动服务器
./bin/data_node 127.0.0.1 9100 /tmp/dkv_test

# 终端 2：使用客户端
./bin/dkv_cli 127.0.0.1 9100
> put hello world
> get hello
> scan "" ""
> quit
```

### 运行测试

```bash
./bin/test_single_node     # RocksDB 读写删扫测试 (8 cases)
./bin/test_codec           # 长度前缀编解码测试 (3 cases)
./bin/test_shard_router    # Range 分片路由测试 (3 cases)
```

### 启动完整集群

```bash
# 启动 PD
./bin/pd_server 127.0.0.1 8100 1 &

# 启动 3 个数据节点（Raft 模式）
# 格式: data_node <api_ip> <api_port> <db_path> <nodeId> <peer1> <peer2> <peer3>
#       每个 peer 格式: nodeId:ip:port

./bin/data_node 127.0.0.1 9100 /tmp/dkv_node1 1 \
    1:127.0.0.1:9101 2:127.0.0.1:9201 3:127.0.0.1:9301 &

./bin/data_node 127.0.0.1 9200 /tmp/dkv_node2 2 \
    1:127.0.0.1:9101 2:127.0.0.1:9201 3:127.0.0.1:9301 &

./bin/data_node 127.0.0.1 9300 /tmp/dkv_node3 3 \
    1:127.0.0.1:9101 2:127.0.0.1:9201 3:127.0.0.1:9301 &

# 连接任意节点使用
./bin/dkv_cli 127.0.0.1 9100
```

---

## 六、学习路线（推荐顺序）

| 步骤 | 内容 | 文件 | 时间 |
|------|------|------|------|
| 1 | 看懂协议定义 | `proto/*.proto` | 0.5h |
| 2 | 理解 RocksDB | `rocks_engine.h/cpp` | 1h |
| 3 | 理解网络编解码 | `codec.h/cpp` | 0.5h |
| 4 | 理解单节点 KV 服务 | `data_server.cpp` + `kv_service.cpp` | 1h |
| 5 | **Raft 理论** | 看动画 + 论文第 5 章 | 4h |
| 6 | **Raft 代码** | `raft_node.h/cpp` 对照论文 | 4h |
| 7 | 理解 Raft 组网层 | `raft_service.h/cpp` | 1h |
| 8 | 理解 Leader 重定向 | `kv_service.cpp` 中的 handlePut | 0.5h |
| 9 | 理解 PD 和分片 | `shard_manager.cpp` + `pd_server.cpp` | 1h |
| 10 | 串起来跑一遍 | 启动集群 + 客户端操作 | 0.5h |
| **合计** | | | **~14h** |

### 第 5 步 Raft 学习资源

1. [Raft 官网动画](https://raft.github.io/) — 可视化理解选举和复制过程
2. [Raft 论文中文翻译](https://github.com/maemual/raft-zh_cn) — 精读第 5 章
3. 关键概念：
   - **Term（任期）**：每选一次 +1，收到更高 term 消息自动退位
   - **Log Entry**：`{term, index, command}`，所有节点日志最终必须一致
   - **Log Matching Property**：同一 index 上 term 相同 → 之前所有条目相同
4. 检验标准：能口头讲清楚"客户端 PUT 一个 key，从 Leader 到数据落盘发生了什么"

### 第 6 步对照论文看代码

| 论文概念 | 代码位置 | 方法/变量 |
|----------|---------|----------|
| Persistent state | raft_node.h:68-70 | `currentTerm_`, `votedFor_`, `log_` |
| Volatile state | raft_node.h:73-74 | `commitIndex_`, `lastApplied_` |
| Leader volatile | raft_node.h:77-78 | `nextIndex_`, `matchIndex_` |
| RequestVote RPC | raft_node.cpp:43 | `handleRequestVote()` |
| AppendEntries RPC | raft_node.cpp:62 | `handleAppendEntries()` |
| Rules for Servers | raft_node.cpp:130-170 | `becomeFollower/Candidate/Leader()` |
| Commit advance | raft_node.cpp:200 | `advanceCommitIndex()` |
| Apply to state machine | raft_node.cpp:215 | `applyEntries()` |

推荐阅读顺序：构造函数 → 状态转换 → `handleAppendEntries`（最核心）→ `handleRequestVote` → `tick()` → `propose()`

---

## 七、项目亮点总结（面试用）

1. **手写 Raft**（raft_node.cpp, 307 行）：Leader 选举 + 日志复制 + commit 推进 + term 管理。对照论文 Figure 2 实现，支持 RequestVote 和 AppendEntries 两个核心 RPC
2. **Range-based 分片**（shard_router.cpp）：按 key 字典序切分，支持 Scan 跨分片操作
3. **PD 调度器**（shard_manager.cpp）：节点注册、心跳检测（10s 超时）、故障节点检测、自动 rebalance
4. **Leader 重定向**（kv_client.cpp）：客户端收到 NOT_LEADER → 更新路由缓存 → 重连正确 Leader → 重试
5. **RocksDB 集成**（rocks_engine.cpp）：LSM-Tree 存储引擎，支持范围扫描
6. **长度前缀编解码**（codec.cpp）：解决 TCP 粘包拆包问题
7. **RAII 管理模式**：智能指针管理 RocksDB 实例生命周期，自定义 deleter 概念可用于连接池

---

## 八、常见问题

### Q: Raft 为什么要随机 election timeout？

防止多个节点同时超时、同时参选、瓜分选票导致选不出 Leader。随机范围 150~300ms，让最先超时的节点先发起选举。

### Q: Leader 怎么知道 Follower 的日志和自己是否一致？

AppendEntries 请求携带 `prevLogIndex` 和 `prevLogTerm`。Follower 检查自己日志的该位置，如果 term 不一致就拒绝。Leader 被拒绝后递减 `nextIndex` 重试，直到找到匹配点。

### Q: 读操作为什么也要走 Leader？

如果不走 Leader，可能从旧 Leader（已失去领导权但还不知道）读到脏数据。本项目读操作直接从 Leader 的 RocksDB 读取，Follower 上的读请求会被重定向到 Leader。

### Q: PD 挂了怎么办？

PD 存的是元数据（shard 路由表），挂了不影响已有数据的读写（客户端有缓存）。但无法注册新节点、无法 rebalance。完善的做法是 PD 也用 Raft 做成 3 节点集群。

### Q: 如何保证消息不丢？

Raft 日志持久化到 RocksDB（未来可改为直接写文件）。Leader 只有在多数节点确认后才 commit 并回复客户端。如果 Leader 在 commit 前宕机，新 Leader 可能没有这条日志，客户端需要重试。
