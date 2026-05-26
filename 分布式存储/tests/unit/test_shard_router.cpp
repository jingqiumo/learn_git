#include <gtest/gtest.h>
#include "shard_router.h"

TEST(ShardRouterTest, BasicRangeLookup) {
    ShardRouter router;

    ShardMap map;
    // Shard 1: ["", "middle")
    ShardMeta s1{1, "", "middle", 1, 1, {1, 2, 3}};
    // Shard 2: ["middle", "")
    ShardMeta s2{2, "middle", "", 1, 1, {1, 2, 3}};

    map.shards = {s1, s2};
    map.version = 1;

    router.loadShardMap(map);

    EXPECT_EQ(router.getShardId("apple"), 1u);    // < "middle"
    EXPECT_EQ(router.getShardId("aaaa"), 1u);     // < "middle"
    EXPECT_EQ(router.getShardId("middle"), 2u);   // >= "middle"
    EXPECT_EQ(router.getShardId("zebra"), 2u);    // >= "middle"
    EXPECT_EQ(router.getShardId("xyz"), 2u);
}

TEST(ShardRouterTest, UnknownKey) {
    ShardRouter router;
    ShardMap map;
    map.version = 0;
    router.loadShardMap(map);

    EXPECT_EQ(router.getShardId("anything"), 0u);
}

TEST(ShardRouterTest, LeaderAddress) {
    ShardRouter router;

    ShardMeta s1{1, "", "m", 1, 100, {100, 101, 102}};
    ShardMap map{{s1}, 1};
    router.loadShardMap(map);

    router.updateLeader(1, 100, "127.0.0.1:9100");
    router.updateLeader(1, 101, "127.0.0.1:9200");
    router.updateLeader(1, 102, "127.0.0.1:9300");

    EXPECT_EQ(router.getLeaderAddr(1), "127.0.0.1:9100");
    EXPECT_EQ(router.getLeaderNodeId(1), 100u);
}
