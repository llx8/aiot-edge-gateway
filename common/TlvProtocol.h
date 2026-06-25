#ifndef TLV_PROTOCOL_H
#define TLV_PROTOCOL_H
#include <cstdint>
#include <vector>
#include <cstring>
#include <arpa/inet.h>

#pragma pack(push, 1)
// 报文头
struct TlvHeader {
    uint16_t magic;     // 0x5A5A, 报文开始
    uint8_t version;    // 版本
    uint16_t total_len; // 总长度
    uint8_t type;       // 报文类型 支持0x01温湿度 0x02开关 0x03模拟量 0x04视觉告警 0xFF心跳
};
#pragma pack(pop)

// 完整报文
struct Tlvpacket {
    TlvHeader header;               // 报文头
    std::vector<uint8_t> value;     // 数据
    uint16_t crc;                   // 校验值
};

// CRC16
uint16_t crc16_modbus(const uint8_t* data, size_t len);

// 编码函数
std::vector<uint8_t> encode_tlv(const Tlvpacket& packet);

// 解码函数
bool decode_tlv(const uint8_t* data, size_t length, Tlvpacket& packet);

#endif