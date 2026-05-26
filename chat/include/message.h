#ifndef MESSAGE_H
#define MESSAGE_H
#include<iostream>
class Message
{
public:
    // 无参构造
    Message();

    // 有参构造
    Message(int msgid, int fromid, int toid, const std::string& content, const std::string& time);

    // get 方法
    int getMsgid() const;
    int getFromid() const;
    int getToid() const;
    std::string getContent() const;
    std::string getTime() const;

    // set 方法
    void setMsgid(int msgid);
    void setFromid(int fromid);
    void setToid(int toid);
    void setContent(const std::string& content);
    void setTime(const std::string& time);

private:
    int msgid;//数据库中信息的id
    int fromid;//发送方的id
    int toid;//接收方的id；
    std::string m;//信息的内容
    std::string time;//发送的时间
};


#endif