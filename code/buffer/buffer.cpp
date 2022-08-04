/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}


/// 返回缓存区剩余的未读数据大小
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

/// 返回剩余的缓存区大小
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

/// 返回缓存区读指针已经读到的位置
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

/// 返回缓存区读指针已经读到的位置下一个数据的地址
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

/// 读指针位置的重定位 len代表本次又读到的长度
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

/// 将读指针的位置移到*end代表的地址处
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

/// 缓存区清零，读指针位置和写指针位置置为0
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

/// 将缓存区没有读到的数据全部读出来，然后缓存区清零，读指针和写指针的位置重置为0
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}


/// 返回剩余可以写的缓存区开始的地址  const
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

/// 返回剩余可以写的缓存区开始的地址
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

/// 缓存区数据更改更新写后数据的写指针的位置
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

/// 在缓存区后面继续追加数据，如果数据太大，就触发扩容
void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

/// 在缓存区后面继续追加数据，如果数据太大，就触发扩容
void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

/// 将str内存中的数据追加拷贝到缓存区中
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

/// 在缓存区后面继续追加数据，如果数据太大，就触发扩容
void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

/// 如果剩余的缓存区大小小于了要写的长度，就触发扩容
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {   /// 如果剩余的缓存区大小小于了要写的长度，就触发扩容
        MakeSpace_(len);      /// 扩容
    }
    assert(WritableBytes() >= len);
}

ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = WritableBytes();   /// 剩余可以写的数量
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;  /// 剩余可以写的缓存区开始的地址
    iov[0].iov_len = writable;                  /// 剩余可以写的缓存区的大小
    iov[1].iov_base = buff;                     /// 扩展空间地址
    iov[1].iov_len = sizeof(buff);              /// 扩展空间大小

    /**
     * https://blog.csdn.net/xiaomiCJH/article/details/75344721
        #include <sys/uio.h>
        ssize_t writev(int sockfd, const struct iovec* iov, int iovcnt);
        ssize_t readn(int sockfd, struct iovec* iov, int iovcnt);
                返回：若成功则为读或者写的字节数，若出错则为-1
    */
    /// readv（）是分散读的意思，意思就是先读到iov[0]中，若iov[0]的空间不够了，就把多出来的数据放入到iov[1]中
    const ssize_t len = readv(fd, iov, 2);
    if(len < 0) {    /// 如果读失败了，saveErrno标志位置为错误
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable) {   /// 如果读到的大小小于缓存区剩余的空间，直接将数据放在缓存区后面
        writePos_ += len;
    }
    else {
        writePos_ = buffer_.size();      /// 可以写的指针移到了缓存区末尾
        Append(buff, len - writable);    ///如果读到的大小大于缓存区剩余的空间，那么先扩容，再将没有放进去的数据放入缓存区中
    }
    return len;    /// 返回读到的字节数
}

ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len;
    return len;
}

char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

/// 扩容
void Buffer::MakeSpace_(size_t len) {

    /// 如果缓存区已经读完了的内存大小加上缓存区剩余内存的大小小于要写的长度，就在缓存区后面直接扩容
    if(WritableBytes() + PrependableBytes() < len) {
        /// 扩容，大小为writePos_ + len + 1 即已经写进去的字节数+扩容大小len
        buffer_.resize(writePos_ + len + 1);
    } 
    else {
        /// 如果缓存区已经读完了的内存大小加上缓存区剩余内存的大小大于等于要写的长度，就将还未读的数据移动到缓存区前面，然后更新写缓存区的指针
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}
