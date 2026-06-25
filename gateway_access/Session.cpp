#include "Session.h"
#include "Logger.h"

Session::Session(int fd, size_t buffer_size)
    : fd_(fd)
    , ring_buffer_(buffer_size)
    , state_(ConnState::CONN_IDLE)
{}

// 返回解析出的完整的协议
std::vector<std::vector<uint8_t>> Session::handle_data(const uint8_t* data, size_t len){
    // 追加数据
    ring_buffer_.append(data, len);

    // 驱动状态机
    std::vector<std::vector<uint8_t>> result;

    while(true){
        bool progressed = false;
        switch(state_) {
            case ConnState::CONN_IDLE:  // 还没找到标识位
                // 找magic
                if(ring_buffer_.available_to_read() < sizeof(uint16_t)){
                    break;// 数据太少了，需要读多点数据再解析
                }
                progressed = true; // 标识位够了可以继续解析
                if(ring_buffer_.read_ptr()[0] == 0x5A && ring_buffer_.read_ptr()[1] == 0x5A){ // 找到了标识位
                    // 读取出来存着
                    ring_buffer_.consume(sizeof(uint16_t));
                    // 改变状态，改解析协议头了
                    state_ = ConnState::CONN_READING_HEADER;
                }
                else{
                    // 不是标识位，给他去掉重新找
                    ring_buffer_.consume(1);
                }
                break;
            case ConnState::CONN_READING_HEADER:
                // 读header
                if (ring_buffer_.available_to_read() < sizeof(TlvHeader) - sizeof(uint16_t)){
                    break;
                }
                else{
                    current_header_.version = ring_buffer_.read_ptr()[0]; 
                    // 需要将网络字节序转成主机字节序
                    uint16_t total_len_net = (ring_buffer_.read_ptr()[1] << 8) | ring_buffer_.read_ptr()[2];
                    current_header_.total_len = ntohs(total_len_net);
                    current_header_.type = ring_buffer_.read_ptr()[3];
                    ring_buffer_.consume(sizeof(TlvHeader) - sizeof(uint16_t));
                    if (current_header_.total_len >= sizeof(TlvHeader) + sizeof(uint16_t) && current_header_.total_len <= kMaxPacketSize){
                        // 正常的包，进入下一环节
                        state_ = ConnState::CONN_READING_BODY;
                    }
                    else{
                        // 不正常的包，直接返回去重新来
                        state_ = ConnState::CONN_IDLE;
                    }
                    progressed = true;
                }
                break;
            case ConnState::CONN_READING_BODY:
                // 读body + 校验
                if (ring_buffer_.available_to_read() < current_header_.total_len - sizeof(TlvHeader)){
                    break;
                }
                else{
                    // 将包解析出来
                    std::vector<uint8_t> full(current_header_.total_len);
                    full[0] = 0x5A;
                    full[1] = 0x5A;
                    full[2] = current_header_.version;
                    uint16_t total_len_net = htons(current_header_.total_len);
                    full[3] = (total_len_net >> 8) & 0xFF;
                    full[4] = total_len_net & 0xFF;
                    full[5] = current_header_.type;
                    memcpy(&full[6], ring_buffer_.read_ptr(), current_header_.total_len - sizeof(TlvHeader));
                    Tlvpacket packet;
                    if(decode_tlv(full.data(), full.size(), packet)){
                        // crc判断
                        result.push_back(full);
                        ring_buffer_.consume(current_header_.total_len - sizeof(TlvHeader));
                        state_ = ConnState::CONN_IDLE;
                    }
                    else{
                        // 假的包
                        GetLogger("gateway")->error("crc check failed");
                        ring_buffer_.consume(current_header_.total_len - sizeof(TlvHeader));
                        state_ = ConnState::CONN_IDLE;
                    }
                    progressed = true;
                }
                break;
        }
        if (!progressed){
            break;
        }
    }
    // 解析成功返回结果
    return result;
}