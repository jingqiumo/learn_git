#ifndef USER_H
#define USER_H
#include<iostream>
#include<string>

class User
{
public:
    User(int id=-1,std::string name="",std::string pwd="",std::string state="offline");

    void setid(int id);
    void setname(std::string name);
    void setpwd(std::string pwd);
    void setstate(std::string state);


    int getid();
    std::string getname();
    std::string getpwd();
    std::string getstate();
private:
    int _userid;//账号id
    std::string _name;//用户名
    std::string _pwd;//密码
    std::string _state;//在线状态
};


#endif