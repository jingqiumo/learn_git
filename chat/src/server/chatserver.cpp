#include"chatserver.h"
#include<functional>
#include"chatservice.h"
using namespace std::placeholders;
ChatServer::ChatServer(EventLoop* loop,const InetAddress& listenadd,const string& name)
                        : _server(loop,listenadd,name)
                        ,_loop(loop)
{
    pool.setMode(POOLMODE::CACHE);
    pool.start();
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection,this,_1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage,this,_1,_2,_3));

    _server.setThreadNum(4);
}
void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    if(conn->connected())
    {
        std::cout<<conn->peerAddress().toIpPort()<<"---online"<<std::endl;
    }else
    {
        std::cout<<conn->peerAddress().toIpPort()<<"---offline"<<std::endl;
        ChatService::getInstance().clientColse(conn);
        conn->shutdown();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr& conn,Buffer* buffer,Timestamp time)
{
    string buf=buffer->retrieveAllAsString();
    json js=json::parse(buf);

    int msgtype=js["msgtype"];

    auto func=ChatService::getInstance().getHandler(msgtype);
    pool.submitTask(func,conn,js,time);
}
