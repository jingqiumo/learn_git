# 个人简历

**求职意向**：C++ 后端开发实习生 | **期望地点**：不限 | **到岗时间**：随时

---

## 教育背景

XX大学 · 计算机科学与技术 · 本科 · 20XX - 20XX
GPA：X.X / 4.0（如有优势可写）

---

## 专业技能

- **编程语言**：熟练掌握 C++（C++17），具备现代 C++ 工程实践经验，熟悉 STL 源码级原理；了解 Python/Shell
- **数据结构与算法**：掌握常用数据结构（红黑树、跳表、B+树、哈希表）及算法，了解其标准库实现
- **Linux 系统编程**：熟悉进程/线程管理、IPC、内存映射、信号处理、I/O 多路复用（epoll），理解 Linux VFS 及 I/O 模型
- **Linux 网络编程**：熟悉 TCP/IP 协议栈，掌握 socket 编程、Reactor/Proactor 模式、select/poll/epoll，了解 muduo 网络库架构
- **数据库**：熟悉 MySQL（InnoDB 索引结构、MVCC 原理、MGR 组复制主从架构），具备 SQL 优化意识
- **工具链**：CMake、GDB、Git、Nginx、Redis、Valgrind

---

## 项目经历

### 分布式即时通讯系统（ChatServer） | 独立开发 | 20XX.XX - 20XX.XX

**技术栈**：C++17 / muduo / MySQL / Redis / Nginx / nlohmann::json / CMake

**项目描述**：
基于 muduo Reactor 模型的即时通讯服务器，支持用户注册登录、一对一实时聊天、离线消息存储与推送、好友管理等功能。配合 Nginx 负载均衡与 Redis pub/sub 实现分布式水平扩展。

**工作内容**：

- 基于 muduo 的 one-loop-per-thread 架构搭建 TCP 服务器，设置 4 个 I/O 工作线程，主线程负责 accept 分发，通过 onConnection/onMessage 回调实现 JSON 协议解析与分发
- 设计并实现 JSON 通信协议（12 种消息类型），覆盖注册登录、实时消息、ACK 确认、好友申请/同意/拒绝/列表查询等完整业务流程
- 实现**动态线程池**（Fixed/Cache 双模式）：Cache 模式下根据任务队列积压自动扩容，空闲超时 10s 自动回收线程；使用 `packaged_task` + `future` 支持带返回值的异步任务提交
- 实现**MySQL 连接池**（单例 + 生产者-消费者）：支持连接自动扩容与空闲回收；所有 SQL 操作使用 Prepared Statement 参数化绑定防止注入；连接借还采用 `unique_ptr` + 自定义 deleter 实现 RAII 自动归还
- 引入 **Redis pub/sub** 实现跨服务器实例消息推送：使用独立线程运行订阅事件循环，订阅/取消订阅通过命令队列异步执行，避免多线程操作 `redisContext` 的竞态条件
- 使用 **Nginx stream 模块**做四层 TCP 负载均衡，将客户端连接分发到多台后端服务器，配合 Redis 实现分布式部署
- 离线消息设计：消息落库标记 `status=0`（未读），用户登录后批量推送并等待客户端 ACK 后标记已读
- 实现**命令行客户端**：通过括号匹配算法处理 TCP 粘包拆包，解析 JSON 协议并自动 ACK 回复
- 编写 CMake 构建系统，使用 GDB 调试内存问题，通过 Valgrind 排查内存泄漏

---

## 个人亮点（面试可展开）

- 深入理解本项目涉及的 C++ 特性：智能指针资源管理、RAII、lambda/callback、模板元编程、多线程同步
- 理解 muduo 网络库核心设计：EventLoop、Poller（epoll 封装）、Channel、Buffer 设计、runInLoop 线程切换机制
- 能讲清楚 MySQL InnoDB 索引原理（B+ 树、聚簇索引 vs 二级索引、覆盖索引、最左前缀）、MVCC（ReadView + undo log）、MGR 组复制协议（Paxos）
- 能讲清楚 TCP 粘包原因及解决方案（定长/分隔符/消息头+消息体），本项目用的 JSON 括号匹配本质是分隔符方案

---

## 自我评价

热爱 C++ 后端技术，关注代码质量与工程规范。具备从网络层到存储层的完整知识体系，有真实项目的踩坑与问题解决经验，对服务端高性能编程有浓厚兴趣，期待在实习中持续成长。
