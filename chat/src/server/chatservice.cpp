#include"chatservice.h"

using namespace std::placeholders;
ChatService& ChatService::getInstance()
{
    static ChatService c;
    return c;
}

FUNC ChatService::getHandler(int msgtype)
{
    auto it=um.find(msgtype);
    if(it==um.end())
    {
        return [=](const TcpConnectionPtr& conn,json& js,Timestamp){
            LOG_ERROR<<"msgid: "<<msgtype<<"can not find";
        };
    }else
    {
        return um[msgtype];
    }
}

void ChatService::login(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int id=js["id"];
    string pwd=js["password"];
    User user=_usermodel.query(id);
    
    if(user.getid()!=-1&&user.getpwd()==pwd)
    {
        if(user.getstate()=="offline")
        {
            {
                std::lock_guard<std::mutex> lock(_connMutex);
                _userIdConn[id]=conn;
            }
            _redis.subscribe(id);
            json re;
            re["msgtype"]=MsgType::LOGIN_ACK;
            re["error"]=0;
            re["name"]=user.getname();
            _usermodel.updateStates(id,"online");
            conn->send(re.dump());
            std::vector<Message> m=_usermodel.getofflineMessage(id);
            for(auto& it:m)
            {
                json URM;
                URM["msgtype"]=MsgType::ONE_MESSAGE;
                URM["messageID"]=it.getMsgid();
                URM["from"]=it.getFromid();
                URM["to"]=id;
                URM["message"]=it.getContent();
                URM["time"]=it.getTime();
                conn->send(URM.dump());
            }
        }else{
            json re;
            re["msgtype"]=MsgType::LOGIN_ACK;
            re["error"]=1;
            re["error_message"]="已登录，不允许重复登陆";
            conn->send(re.dump());
        }
        
    }else
    {
        json re;
        re["msgtype"]=MsgType::LOGIN_ACK;
        re["error"]=1;
        re["error_message"]="账号或密码错误";
        conn->send(re.dump());
    }
}

void ChatService::ref(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    
    std::string name=js["name"]; 
    std::string pwd=js["password"];
    User user;
    user.setname(name);
    user.setpwd(pwd);
    if(_usermodel.insert(user))
    {
        json re;
        re["msgtype"]=MsgType::REF_ACK;
        re["error"]=0;
        re["id"]=user.getid();
        conn->send(re.dump());
    }else
    {
        json re;
        re["msgtype"]=MsgType::REF_ACK;
        re["error"]=1;
        re["error_message"]="注册失败";
        conn->send(re.dump());
    }
    
}

void ChatService::onemessage(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int fromid=js["fromid"];
    int toid=js["toid"];
    std::string message=js["message"];
    Message m=_usermodel.insertMessage(fromid,toid,message);
    if(m.getMsgid()==-1)
        return;
    js["messageID"]=m.getMsgid();
    js["time"]=m.getTime();
    
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it=_userIdConn.find(toid);
        if(it!=_userIdConn.end())
        {
            it->second->send(js.dump());
            return;
        }
    }
    User user=_usermodel.query(toid);
    if(user.getid()!=-1&&user.getstate()=="online")
    {
        _redis.publish(toid,js.dump());
    }

}


void ChatService::clientColse(const TcpConnectionPtr& conn)
{
    int id{};
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        for(auto it=_userIdConn.begin();it!=_userIdConn.end();it++)
        {
            if(it->second==conn)
            {
                id=it->first;
                _userIdConn.erase(it);
                _redis.unsubscribe(id);
                break;
            }
        }
    }
    
    _usermodel.updateStates(id,"offline");
}
void ChatService::ackMessage(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int messageID=js["messageID"];
    _usermodel.updateMesStatus(messageID);
}


void ChatService::addfriend(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int u_id=js["user_id"];
    int f_id=js["friend_id"];
    if(!_usermodel.applyFriend(u_id,f_id))
    {
        json re;
        re["msgid"]=MsgType::ADD_FRIEND_ACK;
        re["error"]=1;
        re["error_message"]="已申请过，对方还未同意";
        conn->send(re.dump());
    }
}

void ChatService::agreefiend(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int u_id=js["user_id"];
    int f_id=js["friend_id"];
    _usermodel.agreeFriend(u_id,f_id);
}
void ChatService::refusefriend(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    
    int u_id=js["user_id"];
    int f_id=js["friend_id"];
    _usermodel.refuseFriend(u_id,f_id);
}
void ChatService::getaddlist(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int u_id=js["user_id"];
    std::vector<FriendApply> f=_usermodel.getApplyList(u_id);
    for(auto& it:f)
    {
        json re;
        re["msgtype"]=MsgType::GET_ADDLIST;
        re["user_id"]=it.userId;
        re["user_name"]=it.userName;
        re["time"]=it.applyTime;
        conn->send(re.dump());
    }
}
void ChatService::getfriendlist(const TcpConnectionPtr& conn,json& js,Timestamp time)
{
    int u_id=js["user_id"];
    std::vector<FriendApply> f=_usermodel.getFriendList(u_id);
    for(auto& it:f)
    {
        json re;
        re["msgtype"]=MsgType::GET_FRILIST;
        re["user_id"]=it.userId;
        re["user_name"]=it.userName;
        re["remark"]=it.remark;
        re["time"]=it.applyTime;
        conn->send(re.dump());
    }
}



ChatService::ChatService()
{
    um.insert({(int)MsgType::LOGIN,std::bind(&ChatService::login,this,_1,_2,_3)});
    um.insert({(int)MsgType::REF,std::bind(&ChatService::ref,this,_1,_2,_3)});
    um.insert({(int)MsgType::ONE_MESSAGE,std::bind(&ChatService::onemessage,this,_1,_2,_3)});
    um.insert({(int)MsgType::ACK_MESSAGE,std::bind(&ChatService::ackMessage,this,_1,_2,_3)});

    um.insert({(int)MsgType::ADD_FRIEND,std::bind(&ChatService::addfriend,this,_1,_2,_3)});
    um.insert({(int)MsgType::AGREE_FRIEND,std::bind(&ChatService::agreefiend,this,_1,_2,_3)});
    um.insert({(int)MsgType::REFUSE_FRIEND,std::bind(&ChatService::refusefriend,this,_1,_2,_3)});
    um.insert({(int)MsgType::GET_ADDLIST,std::bind(&ChatService::getaddlist,this,_1,_2,_3)});
    um.insert({(int)MsgType::GET_FRILIST,std::bind(&ChatService::getfriendlist,this,_1,_2,_3)});
    if(_redis.connect())
    {
        _redis.setMsgCallback(std::bind(&ChatService::redisHandler,this,_1,_2));
        _redis.startSubscribeLoop();
    }
}

void ChatService::redisHandler(int channel,const std::string& msg)
{

    std::lock_guard<std::mutex> lock(_connMutex);
    auto it=_userIdConn.find(channel);
    if(it!=_userIdConn.end())
    {
        it->second->send(msg);
    }
}