/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    /// 关闭此客户端连接，释放内存映射区的资源，关闭客户端的文件描述符
    void Close();

    /// 返回用户的文件描述符编号
    int GetFd() const;

    /// 返回用户的端口
    int GetPort() const;

    /// 返回用户的IP
    const char* GetIP() const;
    
    /// 获取用户连接的地址信息
    sockaddr_in GetAddr() const;
    
    bool process();

    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    static bool isET;
    static const char* srcDir;

    /// C++11新特性，原子类型，描述用户连接的数量
    static std::atomic<int> userCount;
    
private:
   
    int fd_;
    struct  sockaddr_in addr_;

    bool isClose_;
    
    int iovCnt_;

    /**
     * struct iovec {
            void  *iov_base;    // Starting address (内存起始地址）

            size_t iov_len;     // Number of bytes to transfer（这块内存长度）
        };
        struct iovec 结构体定义了一个向量元素，通常这个 iovec 结构体用于一个多元素的数组，对于每一个元素，
        iovec 结构体的字段 iov_base 指向一个缓冲区，这个缓冲区存放的是网络接收的数据（read），或者网络将要发送的数据（write）。
        iovec 结构体的字段 iov_len 存放的是接收数据的最大长度（read），或者实际写入的数据长度（write）
    */
    struct iovec iov_[2];
    
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;
    HttpResponse response_;
};


#endif //HTTP_CONN_H
