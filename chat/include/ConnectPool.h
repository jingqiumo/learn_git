#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H
#include"MysqlDB.h"
#include<string>
#include<fstream>
#include<iostream>
#include<unordered_map>
#include<queue>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<atomic>
#include<thread>
#include<functional>
#include<chrono>

using mysql_ptr=std::unique_ptr<MysqlDB,std::function<void(MysqlDB*)>>;//使用该指针接收连接，析构时可以把资源还给线程池

class ConnectPool{
public:
    //获取连接池对象
    static ConnectPool* getConnectPool();

    //消费连接
    mysql_ptr getconn();

private:
    ConnectPool();
    ~ConnectPool();

    void produceConnect();//生产连接

    void freetimeover();//杀死空闲超过10s的连接


    bool readConf();
    std::string host{};
    std::string user{};
    std::string pass{};
    std::string dbname{};
    int port{};

    std::queue<std::unique_ptr<MysqlDB>> _connnectQueue{};//存放mysql连接
    int _initSize{};//连接池初始大小
    std::atomic<int> _curfreeSize{};//连接池目前的空闲连接数量
    std::atomic<int> _curSize{};//连接池已创建连接数量
    int _maxSize{};//连接池最大大小

    std::mutex _lock;//锁

    std::condition_variable produce;//唤醒连接生产者
    std::condition_variable use;//唤醒连接消费者

    bool _run{};
};
#endif