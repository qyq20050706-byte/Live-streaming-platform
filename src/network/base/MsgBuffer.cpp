#include "MsgBuffer.h"
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>

namespace tmms {
namespace network {

// 高性能读取：使用readv + 栈上缓冲区，避免频繁系统调用和内存分配
ssize_t MsgBuffer::readFd(int fd, int* savedErrno) {
    // 栈上临时缓冲区，64KB
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();

    // 第一块：指向当前writable区域
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    // 第二块：指向栈上缓冲区
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 如果当前writable区域足够大，只使用一块；否则使用两块
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 数据全部读入writable区域
        writerIndex_ += n;
    } else {
        // writable区域不够，部分数据进入了extrabuf
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }

    return n;
}

// 高性能写入：直接调用write发送可读区域数据
ssize_t MsgBuffer::writeFd(int fd, int* savedErrno) {
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) > 0) {
        retrieve(n);
    }
    return n;
}

} // namespace network
} // namespace tmms