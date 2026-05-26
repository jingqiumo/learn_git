#include <gtest/gtest.h>
#include "codec.h"
#include "kv_rpc.pb.h"
#include <muduo/net/Buffer.h>
#include <cstring>
#include <arpa/inet.h>

using namespace muduo::net;

TEST(CodecTest, SendAndParseLengthPrefix) {
    // 构造一个 RpcMessage
    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::PUT_REQUEST);
    msg.mutable_put_request()->set_key("hello");
    msg.mutable_put_request()->set_value("world");
    msg.mutable_put_request()->set_call_id(42);

    std::string serialized;
    msg.SerializeToString(&serialized);

    // 模拟发送（构造长度前缀帧）
    uint32_t len = htonl(serialized.size());
    std::string frame;
    frame.append(reinterpret_cast<const char*>(&len), 4);
    frame += serialized;

    // 模拟接收：放入 Buffer
    Buffer buf;
    buf.append(frame.data(), frame.size());

    // 解析长度前缀
    ASSERT_GE(buf.readableBytes(), 4u);
    uint32_t parsedLen = 0;
    memcpy(&parsedLen, buf.peek(), 4);
    parsedLen = ntohl(parsedLen);
    ASSERT_EQ(parsedLen, serialized.size());

    buf.retrieve(4);
    std::string payload = buf.retrieveAsString(parsedLen);

    // 解析 RpcMessage
    dkv::kv::RpcMessage parsed;
    ASSERT_TRUE(parsed.ParseFromString(payload));
    ASSERT_EQ(parsed.type(), dkv::kv::RpcMessage::PUT_REQUEST);
    ASSERT_EQ(parsed.put_request().key(), "hello");
    ASSERT_EQ(parsed.put_request().value(), "world");
    ASSERT_EQ(parsed.put_request().call_id(), 42u);
}

TEST(CodecTest, EmptyPayload) {
    dkv::kv::RpcMessage msg;
    msg.set_type(dkv::kv::RpcMessage::DELETE_REQUEST);
    msg.mutable_delete_request()->set_key("key");
    msg.mutable_delete_request()->set_call_id(1);

    std::string serialized;
    msg.SerializeToString(&serialized);
    ASSERT_FALSE(serialized.empty());

    // 长度前缀编解码
    uint32_t len = htonl(serialized.size());
    Buffer buf;
    buf.append(&len, 4);
    buf.append(serialized);

    uint32_t parsedLen = 0;
    memcpy(&parsedLen, buf.peek(), 4);
    parsedLen = ntohl(parsedLen);
    buf.retrieve(4);

    std::string payload = buf.retrieveAsString(parsedLen);
    dkv::kv::RpcMessage parsed;
    ASSERT_TRUE(parsed.ParseFromString(payload));
    ASSERT_EQ(parsed.type(), dkv::kv::RpcMessage::DELETE_REQUEST);
}

TEST(CodecTest, PartialFrame) {
    // 模拟不完整的帧，只有部分长度前缀
    Buffer buf;
    uint32_t len = htonl(100);
    buf.append(&len, 2); // 只写了 2 字节，不够

    ASSERT_LT(buf.readableBytes(), 4u);
    // 应该等待更多数据
}
