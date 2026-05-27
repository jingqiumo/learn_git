#pragma once

#include <functional>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Timestamp.h>
#include <google/protobuf/message.h>

// 长度前缀编解码器
// 帧格式: [4字节大端长度(仅protobuf数据长度)] [1字节类型] [protobuf数据]
// 类型: 0x00 = 客户端 RpcMessage, 0x01 = Raft RaftMessage
class LengthPrefixedCodec {
public:
    using MessageCallback = std::function<void(
        const muduo::net::TcpConnectionPtr&,
        const google::protobuf::Message&,
        muduo::Timestamp)>;

    explicit LengthPrefixedCodec(MessageCallback cb)
        : callback_(std::move(cb)) {}

    // 收到数据时调用，解析出完整帧后回调
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp ts);

    // 发送 Protobuf 消息（自动加长度前缀）
    static void send(const muduo::net::TcpConnectionPtr& conn,
                     const google::protobuf::Message& msg);

    // 直接发送原始字节（已编码好的帧）
    static void sendRaw(const muduo::net::TcpConnectionPtr& conn,
                        const std::string& data);

private:
    MessageCallback callback_;
};
