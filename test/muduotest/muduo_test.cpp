#include<iostream>
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<functional>
using namespace muduo;
using namespace muduo::net;

class ChatServer{

public:
    ChatServer(EventLoop* loop,
               const InetAddress& listenAddr,
               const string& nameArg)
               :_server(loop,listenAddr,nameArg),
               _loop(loop)
               {
                    _server.setConnectionCallback([this](const TcpConnectionPtr& conn)
                                                {
                                                    this->onConnection(conn);
                                                });
                    _server.setMessageCallback(std::bind(&ChatServer::onMessage,
                        this,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
                        
                    _server.setThreadNum(4);
               }
    void start()
    {
        _server.start();
    }
private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if(conn->connected())
        {
            std::cout<<conn->peerAddress().toIpPort()<<"online"<<std::endl;
        }else{
            std::cout<<conn->peerAddress().toIpPort()<<"offline"<<std::endl;
            conn->shutdown();//相当于close(fd)
            //_loop->quit();//退出服务器
        }
    }

    void onMessage(const TcpConnectionPtr& conn,Buffer* buffer,Timestamp time)
    {
        std::string buf=buffer->retrieveAllAsString();
        std::cout<<"recv:"<<buf<<" "<<"time:"<<time.toString()<<std::endl;
        conn->send(buf);
    }
    TcpServer _server;
    EventLoop* _loop;
};

int main()
{
    EventLoop loop;
    InetAddress ipport("127.0.0.1",8080);
    ChatServer s(&loop,ipport,"server");

    s.start();
    loop.loop();
    return 0;
}