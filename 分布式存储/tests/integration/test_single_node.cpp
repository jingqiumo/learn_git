#include <gtest/gtest.h>
#include "rocks_engine.h"
#include <cstdio>
#include <thread>
#include <chrono>

// Phase 1 集成测试：直接测试 RocksEngine（不经过网络）

const std::string kTestDbPath = "/tmp/dkv_test_single_node";

class SingleNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 清理旧的测试数据
        std::system(("rm -rf " + kTestDbPath).c_str());
        engine_ = std::make_unique<RocksEngine>(kTestDbPath);
        ASSERT_TRUE(engine_->open());
    }

    void TearDown() override {
        engine_->close();
        engine_.reset();
        std::system(("rm -rf " + kTestDbPath).c_str());
    }

    std::unique_ptr<RocksEngine> engine_;
};

TEST_F(SingleNodeTest, PutAndGet) {
    ASSERT_TRUE(engine_->put("key1", "value1"));
    std::string value;
    ASSERT_TRUE(engine_->get("key1", value));
    EXPECT_EQ(value, "value1");
}

TEST_F(SingleNodeTest, KeyNotFound) {
    std::string value;
    EXPECT_FALSE(engine_->get("nonexistent", value));
    EXPECT_TRUE(value.empty());
}

TEST_F(SingleNodeTest, Delete) {
    ASSERT_TRUE(engine_->put("key1", "value1"));
    ASSERT_TRUE(engine_->del("key1"));
    std::string value;
    EXPECT_FALSE(engine_->get("key1", value));
}

TEST_F(SingleNodeTest, Overwrite) {
    ASSERT_TRUE(engine_->put("key1", "old_value"));
    ASSERT_TRUE(engine_->put("key1", "new_value"));
    std::string value;
    ASSERT_TRUE(engine_->get("key1", value));
    EXPECT_EQ(value, "new_value");
}

TEST_F(SingleNodeTest, Scan) {
    engine_->put("a_key", "a_val");
    engine_->put("b_key", "b_val");
    engine_->put("c_key", "c_val");
    engine_->put("d_key", "d_val");
    engine_->put("e_key", "e_val");

    // 全量扫描
    auto result = engine_->scan("", "", 100);
    EXPECT_EQ(result.kvs.size(), 5u);
    EXPECT_FALSE(result.has_more);

    // 限制数量
    result = engine_->scan("", "", 3);
    EXPECT_EQ(result.kvs.size(), 3u);
    EXPECT_TRUE(result.has_more);

    // 范围扫描 [b, d)
    result = engine_->scan("b_key", "d_key", 100);
    EXPECT_EQ(result.kvs.size(), 2u); // b_key, c_key
}

TEST_F(SingleNodeTest, ScanEmptyDatabase) {
    auto result = engine_->scan("", "", 100);
    EXPECT_EQ(result.kvs.size(), 0u);
    EXPECT_FALSE(result.has_more);
}

TEST_F(SingleNodeTest, BatchWrite) {
    std::vector<std::pair<std::string, std::string>> puts = {
        {"batch_1", "val1"},
        {"batch_2", "val2"},
        {"batch_3", "val3"},
    };
    ASSERT_TRUE(engine_->writeBatch(puts, {}));

    std::string value;
    ASSERT_TRUE(engine_->get("batch_1", value));
    EXPECT_EQ(value, "val1");
    ASSERT_TRUE(engine_->get("batch_2", value));
    EXPECT_EQ(value, "val2");
    ASSERT_TRUE(engine_->get("batch_3", value));
    EXPECT_EQ(value, "val3");
}

TEST_F(SingleNodeTest, LargeValue) {
    std::string large(1024 * 1024, 'X'); // 1MB
    ASSERT_TRUE(engine_->put("large_key", large));
    std::string value;
    ASSERT_TRUE(engine_->get("large_key", value));
    EXPECT_EQ(value.size(), large.size());
    EXPECT_EQ(value, large);
}
