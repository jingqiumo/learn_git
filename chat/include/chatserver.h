#ifndef CHATSERVER
#define CHATSERVER
#include<iostream>
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<nlohmann/json.hpp>
#include"threadpool.h"
using namespace nlohmann;
using namespace muduo;
using namespace muduo::net;

class ChatServer{
public:
    ChatServer(EventLoop* loop,const InetAddress& listenadd,const string& name);
    
    void start();//开启服务器
private:
    void onConnection(const TcpConnectionPtr& conn);//检测用户的连接和断开

    void onMessage(const TcpConnectionPtr& conn,Buffer* buffer,Timestamp);//读取用户发来的信息
    TcpServer _server;
    EventLoop* _loop;
    ThreadPool pool;
};
#endif