#ifndef USERMODEL_H
#define USERMODEL_H
#include"user.h"
#include"ConnectPool.h"
#include"message.h"
#include<vector>

struct FriendApply
{
    int userId;          // 申请人ID
    std::string userName;
    std::string applyTime; // 申请时间
    std::string remark;//备注
};



// 群信息
// struct ChatGroup
// {
//     int groupId;
//     std::string groupName;
//     int ownerId;
//     std::string createTime;
// };

class UserModel
{
public:
    bool insert(User& user);//注册，插入信息
    User query(int id);//根据账号id 查找
    bool updateStates(int id,std::string state);//更新用户在线状态
    Message insertMessage(int fromid,int toid,std::string& message);
    bool updateMesStatus(int messageID);
    std::vector<Message> getofflineMessage(int id);

    //添加好友功能
    bool applyFriend(int u_id,int f_id);
    //同意好友
    bool agreeFriend(int u_id,int f_id);
    //拒绝好友
    bool refuseFriend(int u_id,int f_id);

    //获取申请列表
    std::vector<FriendApply> getApplyList(int u_id);

    //获取好友列表
    std::vector<FriendApply> getFriendList(int u_id);





    // // 创建群
    // bool createGroup(int userId, std::string groupName);

    // // 加入群
    // bool joinGroup(int groupId, int userId);

    // // 获取我加入的所有群
    // std::vector<ChatGroup> getMyGroupList(int userId);

    // // 获取群所有成员
    // std::vector<int> getGroupAllMember(int groupId);

    // // 发送群消息
    // bool insertGroupMsg(int groupId, int sendUid, const std::string& content);


};

#endif