/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"


///数据库连接池类，单例模式
class SqlConnPool {
public:
    static SqlConnPool *Instance();

    MYSQL *GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();

    /// 初始化连接池
    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    /// 数据库连接池的上限
    int MAX_CONN_;

    int useCount_;
    int freeCount_;

    std::queue<MYSQL *> connQue_;   /// 数据库连接池队列
    std::mutex mtx_;                /// 锁
    sem_t semId_;                   /// 信号量
};


#endif // SQLCONNPOOL_H
