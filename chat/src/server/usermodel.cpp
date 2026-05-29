#include"usermodel.h"




bool UserModel::insert(User& user)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    int i{};
    bool ret=d->execute_insert("INSERT INTO user (username,password) VALUES (?,?)",i,user.getname(),user.getpwd());
    
    user.setid(i);
    return ret;
}

User UserModel::query(int id)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    std::vector<std::vector<std::string>> ret=d->query("SELECT * FROM user WHERE id = ?",id);
    
    User u{};
    if(!ret.empty())
    {
        User user(stoi(ret[0][0]),ret[0][1],ret[0][2],ret[0][3]);
        u=user;
    }
    
    return u;

}

bool UserModel::updateStates(int id,std::string state)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    bool ret=d->execute("UPDATE user SET state=? where id=?",state,id);

    return ret;

}


Message UserModel::insertMessage(int fromid,int toid,std::string& message)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    int i{};
    d->execute_insert("INSERT INTO message (fromid, toid, content) VALUES (?,?,?)",i,fromid,toid,message);

    Message m;
    m.setMsgid(i);
    std::vector<std::vector<std::string>> ret=d->query("SELECT createtime FROM message WHERE msgid =  ?",i);

    if(!ret.empty())
    {
        m.setTime(ret[0][0]);
    }

    return m;
}


bool UserModel::updateMesStatus(int messageID)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    bool ret=d->execute("update message set status=1 where msgid=?",messageID);
  
    return ret;
}

std::vector<Message> UserModel::getofflineMessage(int id)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    std::vector<std::vector<std::string>> ret=d->query("select msgid,fromid,content,createtime from message where toid =? and status=0",id);
    
    std::vector<Message> r;
    for(auto& row:ret)
    {
        Message m;
        m.setMsgid(stoi(row[0]));
        m.setFromid(stoi(row[1]));
        m.setContent(row[2]);
        m.setTime(row[3]);
        r.push_back(m);
    }

    return r;
}



bool UserModel::applyFriend(int u_id,int f_id)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    bool ret=d->execute("insert into friend(user_id,friend_id,status) values(?,?,0)",u_id,f_id);
    
    return ret;
}

bool UserModel::agreeFriend(int u_id,int f_id)
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    bool ret=d->execute("update friend set status=1 where user_id=? and friend_id=?",u_id,f_id);
    if(ret)
    {
        bool retc=d->execute("INSERT INTO friend(user_id, friend_id, status) VALUES(?, ?, 1) ON DUPLICATE KEY UPDATE status = 1",
        f_id,u_id);
        return retc;
    }
    
    return ret;
}


bool UserModel::refuseFriend(int u_id,int f_id)
{

    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    bool ret=d->execute("DELETE FROM friend WHERE user_id=? AND friend_id=?",u_id,f_id);

    return ret;
}

std::vector<FriendApply> UserModel::getApplyList(int u_id)
{
    std::vector<FriendApply> f;
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    std::vector<std::vector<std::string>> ret=d->query("SELECT f.user_id, u.username, f.add_time FROM friend f JOIN user u ON f.user_id = u.id WHERE f.friend_id = ? AND status = 0",u_id);
    
    for(auto& row:ret)
    {
        FriendApply fa;
        fa.userId=stoi(row[0]);
        fa.userName=row[1];
        fa.applyTime=row[2];
        f.push_back(fa);
    }

    return f;
}



std::vector<FriendApply> UserModel::getFriendList(int u_id)
{
    std::vector<FriendApply> f;
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();
    std::vector<std::vector<std::string>> ret=d->query("SELECT f.friend_id, u.username, f.remark, f.add_time FROM friend f JOIN user u ON f.friend_id = u.id WHERE f.user_id = ? AND status = 1;",u_id);
    
    for(auto& row:ret)
    {
        FriendApply fa;
        fa.userId=stoi(row[0]);
        fa.userName=row[1];
        fa.remark=row[2];
        fa.applyTime=row[3];
        f.push_back(fa);
        std::cout<<fa.userId<<std::endl;
    }

    return f;
}



// bool UserModel::createGroup(int userId, std::string groupName)
// {
//     std::string groupName=escapeSql(groupName);
    
// }