/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;


/// webserver初始化  在初始化列表中初始化：epoll、定时器、线程池
WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);   //getcwd()会将当前工作目录的绝对路径复制到参数buffer所指的内存空间中,参数maxlen为buffer的空间大小
    assert(srcDir_);                  //如果绝对路径获取失败就退出
    strncat(srcDir_, "/resources/", 16);  //strncat()主要功能是在srcDir_字符串的结尾追加n个字符  资源文件目录

    HttpConn::userCount = 0;              //http连接数量初始化为1
    HttpConn::srcDir = srcDir_;      //http连接的资源目录初始化为srcDir_目录

    /// 数据库连接池初始化，单例模式
    std::cout<<sqlPort<<" "<<sqlUser<<" "<<sqlPwd<<" "<<dbName<<" "<<connPoolNum<<endl;
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    //设置服务器工作模式
    InitEventMode_(trigMode);

    //设置服务器侦听socket，并将侦听socket上epoll树
    if(!InitSocket_()) { isClose_ = true;}

    //初始化记录日志相关参数
    if(openLog) {
        //日志类,单例类  日志的最大长度
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}


///析构函数
WebServer::~WebServer() {
    ///关闭socket侦听描述符
    close(listenFd_);
    isClose_ = true;
    ///释放记录资源目录路径的字符串
    free(srcDir_);
    ///关闭数据库连接池
    SqlConnPool::Instance()->ClosePool();
}

//设置服务器的工作模式
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    ///服务器主循环函数
    while(!isClose_) {
        /// 如果设置了超时事件，就扫描堆上下一个定时器还有多少时间到期，
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        /// epoll等待timeMS （MS）时间，然后处理epoll上的事件
        int eventCnt = epoller_->Wait(timeMS);
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            /// 获取epoll上是哪个事件到期了，返回事件到期的文件描述符
            int fd = epoller_->GetEventFd(i);

            /// 获取到期事件的事件类型
            uint32_t events = epoller_->GetEvents(i);

            /// 如果是服务端侦听描述符有事件发生，就处理侦听事件描述符
            if(fd == listenFd_) {
                DealListen_();
            }
            /// https://blog.csdn.net/q576709166/article/details/8649911?spm=1001.2101.3001.6661.1&utm_medium=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7Edefault-1-8649911-blog-105234862.pc_relevant_default&depth_1-utm_source=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7Edefault-1-8649911-blog-105234862.pc_relevant_default&utm_relevant_index=1
            /// 1）客户端直接调用close，会触犯EPOLLRDHUP事件
            /// 2）通过EPOLLRDHUP属性，来判断是否对端已经关闭，这样可以减少一次系统调用。在2.6.17的内核版本之前，只能再通过调用一次recv函数来判断
            /// 对端正常关闭（程序里close()，shell下kill或ctr+c），触发EPOLLIN和EPOLLRDHUP
            ///
            ///
            ///
            /// EPOLLERR      只有采取动作时，才能知道是否对方异常。即对方突然断掉，是不可能
            /// 有此事件发生的。只有自己采取动作（当然自己此刻也不知道），read，write时，出EPOLLERR错，说明对方已经异常断开。
            /// EPOLLERR 是服务器这边出错（自己出错当然能检测到，对方出错你咋能知道啊）
            ///
            /// 关于 EPOLLERR：
            /// socket能检测到对方出错吗？目前为止，好像我还不知道如何检测。
            /// 但是，在给已经关闭的socket写时，会发生EPOLLERR，也就是说，只有在采取行动（比如
            /// 读一个已经关闭的socket，或者写一个已经关闭的socket）时候，才知道对方是否关闭了。
            /// 这个时候，如果对方异常关闭了，则会出现EPOLLERR，出现Error把对方DEL掉，close就可以
            /// 了。
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);  //关闭用户连接
            }
            /// fd读事件发生了  有新连接请求，对端发送普通数据 触发EPOLLIN。
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);  //处理读数据
            }
            /// fd写事件发生了  EPOLLOUT 有数据要写
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); //处理写数据
            } else { /// 如果是其他类型的事件，则打印错误并忽略
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

/// 1. 定时器到期的回调函数
/// 2. 客户端关闭调用的函数
/// 打印信息，并从epoll树上摘下兴趣列表
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    /// 从epoll树上摘下文件描述符
    epoller_->DelFd(client->GetFd());
    /// 关闭客户端的连接，并释放资源
    client->Close();
}

void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);  /// 初始化用户连接的数组
    if(timeoutMS_ > 0) { /// 如果设置了超时时间，就注册到定时器，并绑定定时器到期事件的回调函数 bind是一个适配器 和下面的lamda表达式一致
        //        timer_->add(fd, timeoutMS_, [&](){
        //            assert(&users_[fd]);
        //            LOG_INFO("Client[%d] quit!", (&users_[fd])->GetFd());
        //            epoller_->DelFd((&users_[fd])->GetFd());
        //            (&users_[fd])->Close();
        //        });
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    /// 上epoll树，并监听读事件
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    /// 设置文件描述符为非阻塞
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

/// 处理监听描述符事件
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        /// 返回连接的文件描述符
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}  /// 如果返回的文件描述符为负数，则客户端与服务端的连接出错，直接返回
        else if(HttpConn::userCount >= MAX_FD) {  /// 如果用户连接的的数量已经大于最大的MAX_FD(65536)，则直接返回
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);     /// 将用户连接文件描述符上epoll树
    } while(listenEvent_ & EPOLLET);   /// 直到事件处理完为止
}

/// 处理客户端发过来的请求
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);  /// 先调整定时器事件堆结构

    /// 线程池添加任务，OnRead_()是绑定回调函数，可转化为lamda形式
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);  /// 先调整定时器事件堆结构

    /// 线程池添加任务，bind是绑定任务处理回调函数，可转化为lamda形式
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

/// 调整文件描述符定时器事件的到期信息，将到期的截止时间重新初始化为timeoutMS_  这里是60s,并调整堆结构
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

/// 服务端读客户端请求回调处理函数，线程池如果有空闲资源会调用此函数
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    /// 服务端读客户端请求，返回读到的字节数和错误码
    ret = client->read(&readErrno);
    /// 如果没有读到任何数据，或者读数据出错了，就关闭客户端连接
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    /// 处理客户端的数据
    OnProcess(client);
}

/// 客户端数据处理类
void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}


/// 服务端写返回客户端响应的回调处理函数
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;

    ret = client->write(&writeErrno);

    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}


///初始化服务端监听socket
/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    //上epoll树
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

/// 设置文件描述符为非阻塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


