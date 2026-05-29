#ifndef MYSQL_H
#define MYSQL_H

#include<mysql/mysql.h>
#include<string>

class MySQL
{
public:
    MySQL();
    //自动关闭连接
    ~MySQL();
    //连接数据库
    bool connect();
    //更新数据库
    bool update(std::string sql);

    //执行查询语句，并返回数据集
    MYSQL_RES* select(std::string sql);

    MYSQL* getConnection();

private:
    
    MYSQL* _conn;
};



#endif