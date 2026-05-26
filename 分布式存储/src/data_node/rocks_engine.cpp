#include "rocks_engine.h"
#include <iostream>

RocksEngine::RocksEngine(const std::string& db_path)
    : db_path_(db_path) {}

RocksEngine::~RocksEngine() {
    close();
}

bool RocksEngine::open() {
    rocksdb::Options opts;
    opts.create_if_missing = true;
    opts.IncreaseParallelism();
    opts.OptimizeLevelStyleCompaction();

    rocksdb::Status s = rocksdb::DB::Open(opts, db_path_, &db_);
    if (!s.ok()) {
        std::cerr << "RocksDB open failed: " << s.ToString() << std::endl;
        return false;
    }
    return true;
}

void RocksEngine::close() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

bool RocksEngine::put(const std::string& key, const std::string& value) {
    rocksdb::WriteOptions opts;
    rocksdb::Status s = db_->Put(opts, key, value);
    return s.ok();
}

bool RocksEngine::get(const std::string& key, std::string& value) {
    rocksdb::ReadOptions opts;
    rocksdb::Status s = db_->Get(opts, key, &value);
    if (s.IsNotFound()) return false;
    return s.ok();
}

bool RocksEngine::del(const std::string& key) {
    rocksdb::WriteOptions opts;
    rocksdb::Status s = db_->Delete(opts, key);
    return s.ok();
}

RocksEngine::ScanResult RocksEngine::scan(const std::string& start_key,
                                           const std::string& end_key,
                                           uint32_t limit) {
    ScanResult result;
    rocksdb::ReadOptions opts;
    opts.auto_prefix_mode = false;
    rocksdb::Iterator* it = db_->NewIterator(opts);

    uint32_t count = 0;
    for (it->Seek(start_key); it->Valid(); it->Next()) {
        if (!end_key.empty() && it->key().ToString() >= end_key) {
            break;
        }
        if (count >= limit) {
            result.has_more = true;
            result.next_key = it->key().ToString();
            break;
        }
        result.kvs.emplace_back(it->key().ToString(), it->value().ToString());
        ++count;
    }
    delete it;
    return result;
}

bool RocksEngine::writeBatch(
        const std::vector<std::pair<std::string, std::string>>& puts,
        const std::vector<std::string>& deletes) {
    rocksdb::WriteBatch batch;
    rocksdb::WriteOptions opts;
    for (auto& p : puts) {
        batch.Put(p.first, p.second);
    }
    for (auto& k : deletes) {
        batch.Delete(k);
    }
    rocksdb::Status s = db_->Write(opts, &batch);
    return s.ok();
}
