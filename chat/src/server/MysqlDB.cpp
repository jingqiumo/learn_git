#include"MysqlDB.h"
#include<iostream>

MysqlDB::MysqlDB()//初始化_conn
{
    _conn=mysql_init(nullptr);
}

bool MysqlDB::connection(const char* host,const char* user,const char* pass,const char* dbname,int port)//连接数据库
{
    if(_conn==nullptr)
    {
        std::cout<<"mysql_init fail"<<std::endl;//初始化失败
        return false;
    }
        

    //连接数据库
    if (!mysql_real_connect(_conn, host, user, pass, dbname, port, NULL, 0)) {
        std::cout << "connect failed: " << mysql_error(_conn) << std::endl;
        mysql_close(_conn);
        return false;
    }
    return true;
}

MysqlDB::~MysqlDB()//关闭数据库连接
{
    if(_conn!=nullptr)
        mysql_close(_conn);
}

Sqlparam MysqlDB::makeparam(const std::string& val)
{
    Sqlparam p{};
    p.type=Sqlparam::STRING_param;
    p.string_val=val;
    return p;
}

Sqlparam MysqlDB::makeparam(const char* val)
{
    Sqlparam p{};
    p.type=Sqlparam::STRING_param;
    p.string_val=val;
    return p;
}
Sqlparam MysqlDB::makeparam(int val)
{
    Sqlparam p{};
    p.type=Sqlparam::INT_param;
    p.int_val=val;
    return p;
}

Sqlparam MysqlDB::makeparam(long val)
{
    Sqlparam p{};
    p.type=Sqlparam::LONG_param;
    p.long_val=val;
    return p;
}

Sqlparam MysqlDB::makeparam(float val)
{
    Sqlparam p{};
    p.type=Sqlparam::FLOAT_param;
    p.float_val=val;
    return p;
}
Sqlparam MysqlDB::makeparam(bool val)
{
    Sqlparam p{};
    p.type=Sqlparam::BOOL_param;
    p.bool_val=val;
    return p;
}
Sqlparam MysqlDB::makeparam(double val)
{
    Sqlparam p{};
    p.type=Sqlparam::DOUBLE_param;
    p.double_val=val;
    return p;
}


//底层真正与数据库接触的部分,防止发生sql注入
bool MysqlDB::execute_base(const char* sql,std::vector<Sqlparam>& param,int* i)
{
    //创建预处理语句对象
    MYSQL_STMT* stmt=mysql_stmt_init(_conn);
    if(!stmt) return false;

    //预编译sql模板(有?占位符)
    if(mysql_stmt_prepare(stmt,sql,strlen(sql)))
    {
        mysql_stmt_close(stmt);
        return false;
    }

    int n=param.size();
    MYSQL_BIND bind[n]{};
    unsigned long len[n]{0};


    //遍历参数，绑定到？
    for(int i=0;i<n;i++)
    {
        auto& p= param[i];
        switch(p.type){
            case Sqlparam::STRING_param:
                bind[i].buffer_type=MYSQL_TYPE_STRING;
                bind[i].buffer=(void*)p.string_val.c_str();
                len[i]=p.string_val.size();
                bind[i].length=&len[i];
                break;
            case Sqlparam::INT_param:
                bind[i].buffer_type=MYSQL_TYPE_LONG;
                bind[i].buffer=&p.int_val;
                break;
            case Sqlparam::BOOL_param:
                bind[i].buffer_type=MYSQL_TYPE_BOOL;
                bind[i].buffer=&p.bool_val;
                break;
            case Sqlparam::FLOAT_param:
                bind[i].buffer_type=MYSQL_TYPE_FLOAT;
                bind[i].buffer=&p.float_val;
                break;
            case Sqlparam::DOUBLE_param:
                bind[i].buffer_type=MYSQL_TYPE_DOUBLE;
                bind[i].buffer=&p.double_val;
                break;
            case Sqlparam::LONG_param:
                bind[i].buffer_type=MYSQL_TYPE_LONGLONG;
                bind[i].buffer=&p.long_val;
                break;
        }
    }


    //绑定参数到预处理语句
    if(n>0)
    {
        mysql_stmt_bind_param(stmt,bind);
    }

    //真正执行mysql语句
    bool ret=mysql_stmt_execute(stmt);

    if(ret==0&&i!=nullptr)
        *i=mysql_stmt_insert_id(stmt);
    //释放预处理资源
    mysql_stmt_close(stmt);
    //返回执行结果
    return !ret;
}



std::vector<std::vector<std::string>> MysqlDB::query_base(const char* sql,std::vector<Sqlparam>& param)
{
    std::vector<std::vector<std::string>> result;
    //创建预处理语句对象
    MYSQL_STMT* stmt=mysql_stmt_init(_conn);
    if(!stmt) return result;

    //预编译sql模板(有?占位符)
    if(mysql_stmt_prepare(stmt,sql,strlen(sql)))
    {
        mysql_stmt_close(stmt);
        return result;
    }

    int n=param.size();
    MYSQL_BIND bind[n]{};
    unsigned long len[n]{0};


    //遍历参数，绑定到？
    for(int i=0;i<n;i++)
    {
        auto& p= param[i];
        switch(p.type){
            case Sqlparam::STRING_param:
                bind[i].buffer_type=MYSQL_TYPE_STRING;
                bind[i].buffer=(void*)p.string_val.c_str();
                len[i]=p.string_val.size();
                bind[i].length=&len[i];
                break;
            case Sqlparam::INT_param:
                bind[i].buffer_type=MYSQL_TYPE_LONG;
                bind[i].buffer=&p.int_val;
                break;
            case Sqlparam::BOOL_param:
                bind[i].buffer_type=MYSQL_TYPE_BOOL;
                bind[i].buffer=&p.bool_val;
                break;
            case Sqlparam::FLOAT_param:
                bind[i].buffer_type=MYSQL_TYPE_FLOAT;
                bind[i].buffer=&p.float_val;
                break;
            case Sqlparam::DOUBLE_param:
                bind[i].buffer_type=MYSQL_TYPE_DOUBLE;
                bind[i].buffer=&p.double_val;
                break;
            case Sqlparam::LONG_param:
                bind[i].buffer_type=MYSQL_TYPE_LONGLONG;
                bind[i].buffer=&p.long_val;
                break;
        }
    }


    //绑定参数到预处理语句
    if(n>0)
    {
        mysql_stmt_bind_param(stmt,bind);
    }

    if(mysql_stmt_execute(stmt)!=0)
    {
        mysql_stmt_close(stmt);
        return result;
    }

    //获取结果元数据（列信息）
    MYSQL_RES* meda=mysql_stmt_result_metadata(stmt);
    int cols=mysql_num_fields(meda);
    //把结果集拉到本地
    mysql_stmt_store_result(stmt);

    MYSQL_BIND rbind[cols]={0};
    char* data[cols];
    unsigned long rlen[cols]={0};


    //为每列分配缓冲区
    for(int i=0;i<cols;i++)
    {
        data[i]=new char[1024];
        rbind[i].buffer_type=MYSQL_TYPE_STRING;
        rbind[i].buffer=data[i];
        rbind[i].buffer_length=1024;
        rbind[i].length=&rlen[i];
    }

    //绑定结果输出位置
    mysql_stmt_bind_result(stmt,rbind);

    //循环读取每一列
    while(mysql_stmt_fetch(stmt)==0)
    {
        std::vector<std::string> row{};
        for(int i=0;i<cols;i++)
        {
            row.push_back(data[i]);
        }
        result.push_back(row);
    }

    for(int i=0;i<cols;i++)
    {
        delete[] data[i];
    }
    mysql_stmt_close(stmt);
    std::cout<<"查询完成"<<std::endl;
    return result;
}


void MysqlDB::setlasttime()
{
    last=std::chrono::high_resolution_clock::now();
}
int MysqlDB::resultfreetime()
{
    std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
    std::chrono::seconds cur = std::chrono::duration_cast<std::chrono::seconds>(now - last);
    return cur.count();
}