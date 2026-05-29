#include"ConnectPool.h"

ConnectPool::ConnectPool()
{
    if(!readConf())
    { 
        throw std::string("配置文件读取失败");
    }
    
    //创建初始连接
    for(int i=0;i<_initSize;i++)
    {
        auto d=std::make_unique<MysqlDB>();
        if(!d->connection(host.c_str(),user.c_str(),pass.c_str(),dbname.c_str(),port))
        {
            throw std::string("数据库连接失败");
        }
        _connnectQueue.push(std::move(d));//添加到队列中
        _curfreeSize++;
        _curSize++;
    }
    _run=true;
    std::thread p(std::bind(&ConnectPool::produceConnect,this));
    p.detach();

    std::thread t(std::bind(&ConnectPool::freetimeover,this));
    t.detach();
        
}
ConnectPool::~ConnectPool()
{
    _run=false;
    produce.notify_all();
}

//生产连接
void ConnectPool::produceConnect()
{
    while(_run)
    {
        std::unique_lock<std::mutex> ulock(_lock);
        produce.wait(ulock,[&](){
            return !_run||(_curfreeSize==0&&_curSize<_maxSize);//只有当前空闲线程为0，并且已创建连接小于最大连接数量
        });

        if(!_run)
            break;
        auto d=std::make_unique<MysqlDB>();
        if(!d->connection(host.c_str(),user.c_str(),pass.c_str(),dbname.c_str(),port))
        {
            std::cerr<<"数据库连接失败"<<std::endl;
            continue;
        }
        _connnectQueue.push(std::move(d));//添加到队列中
        _curfreeSize++;
        _curSize++;
        use.notify_all();
    }

}

//读取配置文件，初始化所需变量
bool ConnectPool::readConf()
{
    std::ifstream inf{"db.conf"};
    if(!inf)
    {
        std::cerr<<"fail"<<std::endl;
        return false;
    }

    std::string strInput{};
    std::unordered_map<std::string,std::string> m{};
    while(std::getline(inf,strInput))
    {
        size_t pos=strInput.find('=');
        if(pos==std::string::npos)
            continue;
        
        std::string key=strInput.substr(0,pos);
        std::string value=strInput.substr(pos+1);
        m[key]=value;
    }
    host=m["ip"];
    port=std::stoi(m["port"]);
    user=m["user"];
    pass=m["password"];
    dbname=m["dbname"];
    _maxSize=std::stoi(m["maxsize"]);
    _initSize=std::stoi(m["initsize"]);

    return true;
}

//单例模式
ConnectPool* ConnectPool::getConnectPool()
{
    try
    {
        static ConnectPool _pool;
        return &_pool;
    }
    catch(std::string& error)
    {
        std::cerr<<error<<std::endl;
        return nullptr;
    }
}

//消费连接
mysql_ptr ConnectPool::getconn()
{
    std::unique_lock<std::mutex> ulock(_lock);
    if(!use.wait_for(ulock,std::chrono::seconds(1),[this](){return _curfreeSize>0;}))
    {
        std::cerr<<"目前任务过多，稍后再试"<<std::endl;
        return nullptr;
    }
    auto up=std::move(_connnectQueue.front());
    _connnectQueue.pop();

    _curfreeSize--;
    produce.notify_all();
    MysqlDB* d=up.release();
    return mysql_ptr{
        d,
        [this](MysqlDB* d){
            std::unique_lock<std::mutex> ulock(_lock);
            d->setlasttime();
            _connnectQueue.push(std::unique_ptr<MysqlDB>(d));
            _curfreeSize++;
            use.notify_one();
        }
    };
}

void ConnectPool::freetimeover()
{
    while(_run)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::unique_lock<std::mutex> ulock(_lock);
        while(!_connnectQueue.empty()&&_curSize>_initSize)
        {
            auto& p=_connnectQueue.front();
            if(p->resultfreetime()>10)
            {
                _connnectQueue.pop();
                _curfreeSize--;
                _curSize--;
            }
            else{
                break;
            }
        }
    }
}
