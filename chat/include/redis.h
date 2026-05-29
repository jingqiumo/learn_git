#ifndef REDIS_H
#define REDIS_H
#include <hiredis/hiredis.h>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <memory>

class RedisClient {
public:
    using MsgCallback = std::function<void(int, const std::string&)>;

    RedisClient();
    ~RedisClient();

    bool connect(const std::string& host = "127.0.0.1", int port = 6379);//连接redis
    bool publish(int channel, const std::string& msg);//发布消息
    bool subscribe(int channel);//订阅已登录ip的消息
    bool unsubscribe(int channel);//取消订阅
    void setMsgCallback(MsgCallback cb);//设置回调函数，将信息传回业务层
    void startSubscribeLoop();//开启队列
    void stopSubscribeLoop();  //关闭队列

private:
    void subLoop();//用来接收队列中的信息

    redisContext* _pubCtx = nullptr;
    redisContext* _subCtx = nullptr;

    MsgCallback _callback;
    std::thread _subThread;
    std::atomic<bool> _running{false};
    std::mutex _pubMutex;

    // 命令队列：将subscribe/unsubscribe请求转交给subLoop线程执行，
    // 确保只有subLoop一个线程操作_subCtx，避免竞态条件导致客户端卡死
    enum class CmdType { SUBSCRIBE, UNSUBSCRIBE };
    struct CmdRequest {
        CmdType type;
        int channel;
        bool done = false;
        bool result = false;
        std::mutex mtx;
        std::condition_variable cv;
    };
    std::queue<std::shared_ptr<CmdRequest>> _cmdQueue;
    std::mutex _cmdMutex;
    std::condition_variable _cmdCv;
};
#endif
