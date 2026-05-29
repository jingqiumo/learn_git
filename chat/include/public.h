#ifndef PUBLIC
#define PUBLIC

enum class MsgType{
    LOGIN=1,//登陆
    REF,//注册
    REF_ACK,//返回注册成功或失败
    LOGIN_ACK,//返回登陆成功或失败
    ONE_MESSAGE,//聊天信息
    ACK_MESSAGE,

    ADD_FRIEND,//添加好友
    ADD_FRIEND_ACK,
    AGREE_FRIEND,//同意申请
    REFUSE_FRIEND,//拒绝申请
    
    GET_ADDLIST,//获取申请好友的列表
    GET_FRILIST//获取好友列表
};

#endif