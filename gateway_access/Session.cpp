#include "Session.h"
#include "Logger.h"

Session::Session(int fd, size_t buffer_size)
    : fd_(fd)
    , ring_buffer_(buffer_size)
    , state_(ConnState::CONN_IDLE)
{}

std::vector<std::vector<uint8_t>> Session::handle_data(const uint8_t* data, size_t len){
    // 追加数据
    ring_buffer_.append(data, len);

    // 驱动状态机
    std::vector<std::vector<uint8_t>> result;

    while(true){
        bool progressed = false;
        switch(state_) {
            case ConnState::CONN_IDLE: 
                // 找magic
                if(ring_buffer_.available_to_read() < sizeof(uint16_t)){
                    break;
                }
                progressed = true;
                if(ring_buffer_.read_ptr()[0] == 0x5A && ring_buffer_.read_ptr()[1] == 0x5A){
                    ring_buffer_.consume(sizeof(uint16_t));
                    state_ = ConnState::CONN_READING_HEADER;
                }
                else{
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
                    uint16_t total_len_net = (ring_buffer_.read_ptr()[1] << 8) | ring_buffer_.read_ptr()[2];
                    current_header_.total_len = ntohs(total_len_net);
                    current_header_.type = ring_buffer_.read_ptr()[3];
                    ring_buffer_.consume(sizeof(TlvHeader) - sizeof(uint16_t));
                    if (current_header_.total_len >= sizeof(TlvHeader) + sizeof(uint16_t) && current_header_.total_len <= kMaxPacketSize){
                        state_ = ConnState::CONN_READING_BODY;
                    }
                    else{
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
                        result.push_back(full);
                        ring_buffer_.consume(current_header_.total_len - sizeof(TlvHeader));
                        state_ = ConnState::CONN_IDLE;
                    }
                    else{
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
    return result;
}