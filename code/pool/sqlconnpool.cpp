/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 

#include "sqlconnpool.h"
using namespace std;

SqlConnPool::SqlConnPool() {
    useCount_ = 0;
    freeCount_ = 0;
}

///  内部静态变量的懒汉单例（C++11 线程安全）
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}


///  初始化数据库连接池 数据库连接池的大小默认是10
void SqlConnPool::Init(const char* host, int port,
            const char* user,const char* pwd, const char* dbName,
            int connSize = 10) {
    assert(connSize > 0);
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);   /// 初始化数据库
        if (!sql) {
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        sql = mysql_real_connect(sql, host,    /// 连接到数据库
                                 user, pwd,
                                 dbName, port, nullptr, 0);
        if (!sql) {
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(sql);                  /// 将已经连接的数据库对象入队列
    }
    MAX_CONN_ = connSize;                      /// 数据库连接池的上限设置为10
    sem_init(&semId_, 0, MAX_CONN_);           /// 初始化信号量值为数据库连接池的上限
}


/// 从数据库连接池获取一个数据库连接，如果没有可用的数据库连接，则等待
MYSQL* SqlConnPool::GetConn() {
    MYSQL *sql = nullptr;
    if(connQue_.empty()){
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    /// sem_wait是一个函数，也是一个原子操作，它的作用是从信号量的值减去一个“1”，但它永远会先等待该信号量为一个非零值才开始做减法
    sem_wait(&semId_);
    {
        lock_guard<mutex> locker(mtx_);
        sql = connQue_.front();
        connQue_.pop();
    }
    return sql;
}

/// 释放一个数据库连接，空闲数据库连接池队列加一，信号量加一
void SqlConnPool::FreeConn(MYSQL* sql) {
    assert(sql);
    lock_guard<mutex> locker(mtx_);
    connQue_.push(sql);
    ///  sem_post是给信号量的值加上一个“1”，它是一个“原子操作”
    sem_post(&semId_);
}


/// 关闭数据库连接池，释放所有的连接
void SqlConnPool::ClosePool() {
    lock_guard<mutex> locker(mtx_);
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    /// 断开数据库连接
    mysql_library_end();        
}

/// 返回数据库连接池可用的数量
int SqlConnPool::GetFreeConnCount() {
    lock_guard<mutex> locker(mtx_);
    return connQue_.size();
}


/// 关闭数据库连接池，释放所有的连接
SqlConnPool::~SqlConnPool() {
    ClosePool();
}
