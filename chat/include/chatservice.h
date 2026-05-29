#ifndef CHATSERVICE
#define CHATSERVICE

#include<iostream>
#include<nlohmann/json.hpp>
using namespace nlohmann;
#include<unordered_map>

#include<functional>
#include<muduo/net/EventLoop.h>
#include<muduo/net/TcpServer.h>
#include<muduo/base/Logging.h>
using namespace muduo;
using namespace muduo::net;
using FUNC=std::function<void(const TcpConnectionPtr& conn,json& js,Timestamp)>;
#include"public.h"
#include"user.h"
#include"usermodel.h"
#include"message.h"
#include"redis.h"
#include<unordered_map>
#include<mutex>
class ChatService{
public:
    

    static ChatService& getInstance();
    void login(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void ref(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void onemessage(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void ackMessage(const TcpConnectionPtr& conn,json& js,Timestamp time);//客户端确定收到消息，更新这条消息的状态改为已读

    void addfriend(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void agreefiend(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void refusefriend(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void getaddlist(const TcpConnectionPtr& conn,json& js,Timestamp time);
    void getfriendlist(const TcpConnectionPtr& conn,json& js,Timestamp time);
    
    FUNC getHandler(int msgid);//根据客户端发来信息的msgtype返回对应函数
    void clientColse(const TcpConnectionPtr& conn);

    void redisHandler(int channel,const std::string& msg);
private:
    ChatService();
    std::unordered_map<int,FUNC> um;
    UserModel _usermodel;
    std::unordered_map<int,TcpConnectionPtr> _userIdConn;

    std::mutex _connMutex;

    RedisClient _redis;
};

#endif