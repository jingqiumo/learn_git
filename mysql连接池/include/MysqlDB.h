#ifndef MYSQL_DB
#define MYSQL_DB
#include<mysql.h>
#include<string>
#include<cstring>
#include<vector>
#include<chrono>

//sql参数容器
struct Sqlparam{
    enum SQL_type{
        INT_param,
        STRING_param,
        LONG_param,
        DOUBLE_param,
        BOOL_param,
        FLOAT_param
    } type;

    int int_val;
    std::string string_val;
    long long_val;
    double double_val;
    bool bool_val;
    float float_val;
};

class MysqlDB{
public:
    MysqlDB();//初始化_conn
    ~MysqlDB();//关闭数据库连接
    //增删改部分
    template<typename... Args>
    bool execute(const char* sql,Args&&... args)
    {
        std::vector<Sqlparam> param{};
        ((param.push_back(makeparam(std::forward<Args>(args)))),...);//判断数据类型并添加到数组中
        return execute_base(sql,param); 
    }

    //查询部分
    template<typename... Args>
    std::vector<std::vector<std::string>> query(const char* sql,Args&&... args)
    {
        std::vector<Sqlparam> param{};
        ((param.push_back(makeparam(std::forward<Args>(args)))),...);
        return query_base(sql,param); 
    }

    bool connection(const char* host,const char* user,const char* pass,const char* dbname,int port);//连接数据库

    void setlasttime();
    int resultfreetime();
private:
    
    

    Sqlparam makeparam(const std::string& val);
    Sqlparam makeparam(const char* val);
    Sqlparam makeparam(int val);
    Sqlparam makeparam(long val);
    Sqlparam makeparam(float val);
    Sqlparam makeparam(bool val);
    Sqlparam makeparam(double val);

    //底层真正与数据库接触的部分,防止发生sql注入
    bool execute_base(const char* sql,std::vector<Sqlparam>& param);
    std::vector<std::vector<std::string>> query_base(const char* sql,std::vector<Sqlparam>& param);


    MYSQL* _conn;
    
    std::chrono::high_resolution_clock::time_point last{};
};




#endif