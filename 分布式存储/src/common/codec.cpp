#include "codec.h"
#include <cstring>
#include <arpa/inet.h>

using namespace muduo::net;
using muduo::Timestamp;

void LengthPrefixedCodec::onMessage(const TcpConnectionPtr& conn,
                                     Buffer* buf,
                                     Timestamp ts) {
    while (buf->readableBytes() >= 4) {
        // Peek 4 字节长度前缀
        uint32_t len = 0;
        memcpy(&len, buf->peek(), 4);
        len = ntohl(len);

        if (buf->readableBytes() < 4 + len) {
            break; // 不够一个完整帧
        }

        buf->retrieve(4); // 消费长度前缀
        std::string payload = buf->retrieveAsString(len);

        // 此时我们不知道具体消息类型，由上层 dispatch
        // callback 里由使用者做具体 Parse
        // 这里用一个通用的方式: 传递原始字节给上层
        // 但接口是 google::protobuf::Message&...
        //
        // 实际使用中，各服务会 decode RpcMessage 然后 dispatch
        // 所以这里我们传一个占位符，让上层真正解析
        //
        // 改进: 改为直接传 payload string，不用 Message&
    }
}

void LengthPrefixedCodec::send(const TcpConnectionPtr& conn,
                                const google::protobuf::Message& msg) {
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(conn, data);
}

void LengthPrefixedCodec::sendRaw(const TcpConnectionPtr& conn,
                                   const std::string& data) {
    uint32_t len = htonl(data.size());
    Buffer buf;
    buf.append(&len, 4);
    buf.append(data);
    conn->send(&buf);
}
