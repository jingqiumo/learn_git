#include <iostream>
#include"MysqlDB.h"
#include<fstream>
#include<unordered_map>
#include"ConnectPool.h"
int main()
{
    ConnectPool* pool=ConnectPool::getConnectPool();
    mysql_ptr d=pool->getconn();

    bool re=d->execute("insert into student(name,age) values(?,?)","hsj",22);
    std::cout<<re<<std::endl;

    // std::vector<std::vector<std::string>> re=d.query("select * from student where age > ?",10);

    // for(auto& a: re)
    // {
    //     std::cout<<"id: "<<a[0]<<" name: "<<a[1]<<" age: "<<a[2]<<std::endl;
    // }
    // return 0;


    
}