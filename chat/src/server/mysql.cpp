#include"mysql.h"
#include<iostream>


MySQL::MySQL()
{
    _conn=mysql_init(nullptr);
    if(!_conn)
    {
        std::cerr<<"mysql 初始化连接失败"<<std::endl;
        exit(-1);
    }
    mysql_set_character_set(_conn,"utf8");
}
 //自动关闭连接
MySQL::~MySQL()
{
    if(_conn!=nullptr)
        mysql_close(_conn);
}
//连接数据库
bool MySQL::connect()
{
    
    if(!mysql_real_connect(
        _conn,
        "127.0.0.1",
        "hsjr",
        "123456",
        "chat",
        3306,
        nullptr,0
    ))
    {
        std::cerr<<"数据库连接失败"<<mysql_error(_conn)<<std::endl;
        return false;
    }else
    {
        std::cout<<"数据库连接成功"<<std::endl;
        return true;
    }
}
//更新数据库
bool MySQL::update(std::string sql)
{
    if(mysql_query(_conn,sql.c_str()))
    {
        std::cerr<<"执行失败："<<sql<<std::endl;
        std::cerr<<"错误"<<mysql_error(_conn)<<std::endl;
        return false;
    }
    return true;

}

//执行查询语句，并返回数据集
MYSQL_RES* MySQL::select(std::string sql)
{
    if(mysql_query(_conn,sql.c_str()))
    {
        std::cerr<<"执行失败："<<sql<<std::endl;
        
        return nullptr;
    }
    return mysql_store_result(_conn);
}

MYSQL* MySQL::getConnection()
{
    return _conn;
}