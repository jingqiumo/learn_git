#include"message.h"


// 无参构造
Message::Message() 
    : msgid(-1), fromid(-1), toid(-1)
{
}

// 有参构造
Message::Message(int msgid, int fromid, int toid, const std::string& content, const std::string& time)
    : msgid(msgid), fromid(fromid), toid(toid), m(content), time(time)
{
}

// ==================== get ====================
int Message::getMsgid() const
{
    return msgid;
}

int Message::getFromid() const
{
    return fromid;
}

int Message::getToid() const
{
    return toid;
}

std::string Message::getContent() const
{
    return m;
}

std::string Message::getTime() const
{
    return time;
}

// ==================== set ====================
void Message::setMsgid(int msgid)
{
    this->msgid = msgid;
}

void Message::setFromid(int fromid)
{
    this->fromid = fromid;
}

void Message::setToid(int toid)
{
    this->toid = toid;
}

void Message::setContent(const std::string& content)
{
    this->m = content;
}

void Message::setTime(const std::string& time)
{
    this->time = time;
}