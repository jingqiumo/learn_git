#pragma once

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <string>
#include <vector>
#include <memory>

// Phase 1: 单节点单 shard 的 RocksDB 封装
// Phase 2/3: 扩展为多 column family（每 shard 一个 CF）或 key 前缀隔离
class RocksEngine {
public:
    explicit RocksEngine(const std::string& db_path);
    ~RocksEngine();

    RocksEngine(const RocksEngine&) = delete;
    RocksEngine& operator=(const RocksEngine&) = delete;

    bool open();
    void close();

    // 基本 KV 操作
    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);

    // 范围扫描 [start_key, end_key)，返回最多 limit 条
    // 返回 pair<kvs, has_more>
    struct ScanResult {
        std::vector<std::pair<std::string, std::string>> kvs;
        bool has_more = false;
        std::string next_key;
    };
    ScanResult scan(const std::string& start_key,
                    const std::string& end_key,
                    uint32_t limit);

    // 批量写入，保证原子性
    bool writeBatch(const std::vector<std::pair<std::string, std::string>>& puts,
                    const std::vector<std::string>& deletes);

    rocksdb::DB* raw() { return db_; }

private:
    std::string db_path_;
    rocksdb::DB* db_ = nullptr;
};
