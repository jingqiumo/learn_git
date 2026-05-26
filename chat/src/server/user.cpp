#include"user.h"

User::User(int id,std::string name,std::string pwd,std::string state)
{
    this->_userid=id;
    this->_name=name;
    this->_pwd=pwd;
    this->_state=state;
}

void User::setid(int id)
{
    this->_userid=id;
}

void User::setname(std::string name)
{
    this->_name=name;
}
void User::setpwd(std::string pwd)
{
    this->_pwd=pwd;
}
void User::setstate(std::string state)
{
    this->_state=state;
}

int User::getid()
{
    return _userid;
}
std::string User::getname()
{
    return _name;
}
std::string User::getpwd()
{
    return _pwd;
}
std::string User::getstate()
{
    return _state;
}