/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 

#ifndef BUFFER_H
#define BUFFER_H
#include <cstring>   //perror
#include <iostream>
#include <unistd.h>  // write
#include <sys/uio.h> //readv
#include <vector> //readv
#include <atomic>
#include <assert.h>
class Buffer {
public:
    /// 初始化buffer的大小为1024
    Buffer(int initBuffSize = 1024);
    /// 禁止掉了默认的析构函数
    ~Buffer() = default;

    /// 返回剩余可以写的字节数
    size_t WritableBytes() const;

    /// 返回剩余可以读的字节数
    size_t ReadableBytes() const ;

    /// 返回剩余可以读的字节的位置
    size_t PrependableBytes() const;

    const char* Peek() const;
    void EnsureWriteable(size_t len);
    void HasWritten(size_t len);

    void Retrieve(size_t len);
    void RetrieveUntil(const char* end);

    void RetrieveAll() ;
    std::string RetrieveAllToStr();

    const char* BeginWriteConst() const;
    char* BeginWrite();

    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff);

    /// 从fd中读数据，放入到缓存区中，若缓存区剩余大小不够，就触发扩容
    ssize_t ReadFd(int fd, int* Errno);
    ssize_t WriteFd(int fd, int* Errno);

private:
    char* BeginPtr_();                  /// 返回buffer缓冲区的头指针
    const char* BeginPtr_() const;      /// 返回buffer缓冲区的头指针  const
    void MakeSpace_(size_t len);

    /// buffer缓冲区，这里利用了vector容器，方便可以动态的增加空间
    std::vector<char> buffer_;
    /// 原子操作，读指针的位置
    std::atomic<std::size_t> readPos_;
    /// 原子操作，写指针的位置
    std::atomic<std::size_t> writePos_;
};

#endif //BUFFER_H
