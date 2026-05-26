#include "redis.h"
#include <iostream>
#include <poll.h>

RedisClient::RedisClient() {}
RedisClient::~RedisClient() {
    stopSubscribeLoop();
}

bool RedisClient::connect(const std::string& host, int port) {
    _pubCtx = redisConnect(host.c_str(), port);
    _subCtx = redisConnect(host.c_str(), port);
    return _pubCtx && _subCtx && !_pubCtx->err && !_subCtx->err;
}

bool RedisClient::publish(int channel, const std::string& msg) {
    std::lock_guard<std::mutex> lock(_pubMutex);
    if (!_pubCtx) return false;
    redisReply* r = (redisReply*)redisCommand(_pubCtx, "PUBLISH %d %s", channel, msg.c_str());
    if (!r) return false;
    freeReplyObject(r);
    return true;
}

bool RedisClient::subscribe(int channel) {
    if (!_running) return false;
    auto req = std::make_shared<CmdRequest>();
    req->type = CmdType::SUBSCRIBE;
    req->channel = channel;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _cmdQueue.push(req);
    }
    _cmdCv.notify_one();

    std::unique_lock<std::mutex> lock(req->mtx);
    if (!req->cv.wait_for(lock, std::chrono::seconds(3), [&req]{ return req->done; })) {
        return false;
    }
    return req->result;
}

bool RedisClient::unsubscribe(int channel) {
    if (!_running) return false;
    auto req = std::make_shared<CmdRequest>();
    req->type = CmdType::UNSUBSCRIBE;
    req->channel = channel;
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        _cmdQueue.push(req);
    }
    _cmdCv.notify_one();

    std::unique_lock<std::mutex> lock(req->mtx);
    if (!req->cv.wait_for(lock, std::chrono::seconds(3), [&req]{ return req->done; })) {
        return false;
    }
    return req->result;
}

void RedisClient::setMsgCallback(MsgCallback cb) {
    _callback = move(cb);
}

void RedisClient::subLoop() {
    while (_running) {
        // 1. 处理pending的subscribe/unsubscribe命令
        //    只有subLoop线程操作_subCtx，不会产生竞态条件
        {
            std::lock_guard<std::mutex> lock(_cmdMutex);
            while (!_cmdQueue.empty()) {
                auto req = _cmdQueue.front();
                _cmdQueue.pop();

                redisReply* r = nullptr;
                if (req->type == CmdType::SUBSCRIBE) {
                    r = (redisReply*)redisCommand(_subCtx, "SUBSCRIBE %d", req->channel);
                } else {
                    r = (redisReply*)redisCommand(_subCtx, "UNSUBSCRIBE %d", req->channel);
                }
                {
                    std::lock_guard<std::mutex> reqLock(req->mtx);
                    req->result = (r != nullptr);
                    req->done = true;
                }
                req->cv.notify_one();
                if (r) freeReplyObject(r);
            }
        }

        if (!_subCtx) break;

        // 2. 用poll带超时等待数据，超时后回到步骤1检查命令队列
        struct pollfd pfd;
        pfd.fd = _subCtx->fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) break;
        if (ret == 0) continue;

        // 3. 有数据到达，读取并处理
        redisReply* reply = nullptr;
        if (redisGetReply(_subCtx, (void**)&reply) != REDIS_OK) {
            break;
        }

        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
            std::string type = reply->element[0]->str;
            if (type == "message") {
                int ch = atoi(reply->element[1]->str);
                std::string msg = reply->element[2]->str;
                if (_callback) _callback(ch, msg);
            }
        }
        if (reply) freeReplyObject(reply);
    }

    // 退出时通知所有未处理的命令失败
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        while (!_cmdQueue.empty()) {
            auto req = _cmdQueue.front();
            _cmdQueue.pop();
            std::lock_guard<std::mutex> reqLock(req->mtx);
            req->result = false;
            req->done = true;
            req->cv.notify_one();
        }
    }
}

void RedisClient::startSubscribeLoop() {
    if (_running) return;
    _running = true;
    _subThread = std::thread(&RedisClient::subLoop, this);
}

void RedisClient::stopSubscribeLoop() {
    if (!_running) return;

    _running = false;
    _cmdCv.notify_one();

    if (_subCtx) {
        redisFree(_subCtx);
        _subCtx = nullptr;
    }

    if (_subThread.joinable()) {
        _subThread.join();
    }

    if (_pubCtx) {
        redisFree(_pubCtx);
        _pubCtx = nullptr;
    }
}
