/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../http/httpconn.h"

class WebServer {
public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~WebServer();
    void Start();

private:
    bool InitSocket_(); 
    void InitEventMode_(int trigMode);
    void AddClient_(int fd, sockaddr_in addr);
  
    void DealListen_();
    void DealWrite_(HttpConn* client);
    void DealRead_(HttpConn* client);

    void SendError_(int fd, const char*info);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess(HttpConn* client);

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool openLinger_;
    int timeoutMS_;  /* 毫秒MS */   /// 服务端超时事件，单位毫秒ms，这个初始化为600000ms = 60s
    bool isClose_;                 /// 服务器运行状态标志
    int listenFd_;                 /// 监听socket文件描述符
    char* srcDir_;                 /// 资源目录
    
    uint32_t listenEvent_;
    uint32_t connEvent_;
   
    ///全部封装为unique_ptr，为了保证使用C++的RAII特性
    std::unique_ptr<HeapTimer> timer_;          /// 定时器事件处理类
    std::unique_ptr<ThreadPool> threadpool_;    /// 线程池类
    std::unique_ptr<Epoller> epoller_;          /// epoll处理类
    std::unordered_map<int, HttpConn> users_;   /// 用户连接数组利用了哈希map，查找更高效
};


#endif //WEBSERVER_H
