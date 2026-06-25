#ifndef __SESSION_H__
#define __SESSION_H__ 

#include "RingBuffer.h"
#include "TlvProtocol.h"
#include <vector>
#include <cstdint>

static constexpr size_t kRingBufferSize = 65536;
constexpr size_t kMaxPacketSize = 4096;

enum class ConnState {
    CONN_IDLE,               // 找magic 0x5A5A
    CONN_READING_HEADER,     // 凑齐6字节Header
    CONN_READING_BODY        // 凑齐total_len 字节
};

class Session {
public:
    // 绑定socket fd
    explicit Session(int fd, size_t buffer_size = kRingBufferSize);
    // 返回解出的完整的tlv数据
    std::vector<std::vector<uint8_t>> handle_data(const uint8_t* data, size_t len);
private:
    int fd_;
    RingBuffer ring_buffer_;
    ConnState state_;
    TlvHeader current_header_;
};

#endif