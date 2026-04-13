// 提取自 muduo Buffer，并移除了所有 muduo 公共头文件的依赖
// 类名修改为 MsgBuffer，位于 tmms::network 命名空间内
#ifndef TMMS_NETWORK_MSGBUFFER_H
#define TMMS_NETWORK_MSGBUFFER_H

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

namespace tmms {
namespace network {

/// 一个简单的网络应用层缓冲区，类似 muduo::net::Buffer
/// 支持自动扩容、零拷贝读取、预留头部空间等特性
class MsgBuffer {
public:
    static const size_t kCheapPrepend = 8;     // 预留头部大小
    static const size_t kInitialSize = 1024;   // 初始缓冲区大小

    explicit MsgBuffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend) {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }

    // 默认拷贝构造、赋值和析构函数可用
    MsgBuffer(const MsgBuffer&) = default;
    MsgBuffer& operator=(const MsgBuffer&) = default;
    ~MsgBuffer() = default;

    // 交换两个缓冲区
    void swap(MsgBuffer& rhs) {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }

    // ==================== 容量查询 ====================
    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }
    size_t capacity() const { return buffer_.size(); }

    // ==================== 数据访问（零拷贝） ====================
    const char* peek() const { return begin() + readerIndex_; }
    const char* data() const { return peek(); }
    size_t size() const { return readableBytes(); }
    bool empty() const { return readableBytes() == 0; }

    // 查找CRLF（用于HTTP等行协议）
    const char* findCRLF() const {
        const char* start = peek();
        const char* end = beginWrite();
        for (const char* p = start; p < end - 1; ++p) {
            if (*p == '\r' && *(p + 1) == '\n') {
                return p;
            }
        }
        return nullptr;
    }

    const char* findCRLF(const char* start) const {
        assert(peek() <= start);
        assert(start <= beginWrite());
        if (start >= beginWrite() - 1) return nullptr;
        for (const char* p = start; p < beginWrite() - 1; ++p) {
            if (*p == '\r' && *(p + 1) == '\n') {
                return p;
            }
        }
        return nullptr;
    }

    const char* findEOL() const {
        const void* eol = memchr(peek(), '\n', readableBytes());
        return static_cast<const char*>(eol);
    }

    const char* findEOL(const char* start) const {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const void* eol = memchr(start, '\n', beginWrite() - start);
        return static_cast<const char*>(eol);
    }

    // ==================== 数据消费 ====================
    void retrieve(size_t len) {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveUntil(const char* end) {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }

    void retrieveInt64() { retrieve(sizeof(int64_t)); }
    void retrieveInt32() { retrieve(sizeof(int32_t)); }
    void retrieveInt16() { retrieve(sizeof(int16_t)); }
    void retrieveInt8() { retrieve(sizeof(int8_t)); }

    void retrieveAll() {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    // 消费数据并返回string
    std::string retrieveAllAsString() {
        return retrieveAsString(readableBytes());
    }

    std::string retrieveAsString(size_t len) {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    std::string toString() const {
        return std::string(peek(), readableBytes());
    }

    // ==================== 数据写入 ====================
    void append(const char* data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    void append(const std::string& str) {
        append(str.data(), str.size());
    }

    void append(const void* data, size_t len) {
        append(static_cast<const char*>(data), len);
    }

    // 确保可写空间
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    char* beginWrite() { return begin() + writerIndex_; }
    const char* beginWrite() const { return begin() + writerIndex_; }

    void hasWritten(size_t len) {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }

    void unwrite(size_t len) {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    // ==================== 网络字节序整数操作 ====================
    void appendInt64(int64_t x) {
        int64_t be64 = htobe64(x);
        append(&be64, sizeof(be64));
    }

    void appendInt32(int32_t x) {
        int32_t be32 = htobe32(x);
        append(&be32, sizeof(be32));
    }

    void appendInt16(int16_t x) {
        int16_t be16 = htobe16(x);
        append(&be16, sizeof(be16));
    }

    void appendInt8(int8_t x) {
        append(&x, sizeof(x));
    }

    int64_t readInt64() {
        int64_t result = peekInt64();
        retrieveInt64();
        return result;
    }

    int32_t readInt32() {
        int32_t result = peekInt32();
        retrieveInt32();
        return result;
    }

    int16_t readInt16() {
        int16_t result = peekInt16();
        retrieveInt16();
        return result;
    }

    int8_t readInt8() {
        int8_t result = peekInt8();
        retrieveInt8();
        return result;
    }

    int64_t peekInt64() const {
        assert(readableBytes() >= sizeof(int64_t));
        int64_t be64 = 0;
        ::memcpy(&be64, peek(), sizeof(be64));
        return be64toh(be64);
    }

    int32_t peekInt32() const {
        assert(readableBytes() >= sizeof(int32_t));
        int32_t be32 = 0;
        ::memcpy(&be32, peek(), sizeof(be32));
        return be32toh(be32);
    }

    int16_t peekInt16() const {
        assert(readableBytes() >= sizeof(int16_t));
        int16_t be16 = 0;
        ::memcpy(&be16, peek(), sizeof(be16));
        return be16toh(be16);
    }

    int8_t peekInt8() const {
        assert(readableBytes() >= sizeof(int8_t));
        int8_t x = *peek();
        return x;
    }

    // ==================== 协议头预填 ====================
    void prepend(const void* data, size_t len) {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        std::copy(d, d + len, begin() + readerIndex_);
    }

    void prependInt64(int64_t x) {
        int64_t be64 = htobe64(x);
        prepend(&be64, sizeof(be64));
    }

    void prependInt32(int32_t x) {
        int32_t be32 = htobe32(x);
        prepend(&be32, sizeof(be32));
    }

    void prependInt16(int16_t x) {
        int16_t be16 = htobe16(x);
        prepend(&be16, sizeof(be16));
    }

    void prependInt8(int8_t x) {
        prepend(&x, sizeof(x));
    }

    // ==================== IO操作 ====================
    // 从文件描述符读取数据（使用readv实现高效读取）
    ssize_t readFd(int fd, int* savedErrno);

    // 向文件描述符写入数据
    ssize_t writeFd(int fd, int* savedErrno);

    // ==================== 内存整理 ====================
    // 收缩预留空间，释放内存
    void shrink(size_t reserve) {
        MsgBuffer other;
        other.ensureWritableBytes(readableBytes() + reserve);
        other.append(toString());
        swap(other);
    }

private:
    char* begin() { return buffer_.data(); }
    const char* begin() const { return buffer_.data(); }

    // 内部扩容逻辑
    void makeSpace(size_t len) {
        // 如果当前可写空间 + 预留空间不够，则扩容
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            buffer_.resize(writerIndex_ + len);
        } else {
            // 否则只需要将已读数据移动到头部
            assert(kCheapPrepend < readerIndex_);
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }

private:
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};

} // namespace network
} // namespace tmms

#endif // TMMS_NETWORK_MSGBUFFER_H