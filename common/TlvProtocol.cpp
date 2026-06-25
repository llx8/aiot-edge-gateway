#include "TlvProtocol.h"
#include <cstdint>

// CRC16/MODBUS手写算法实现
uint16_t crc16_modbus(const uint8_t* data, size_t len){
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++){
        crc ^= data[i];
        for (int j = 0; j < 8; j++){
            if (crc & 0x0001){
                crc = (crc >> 1) ^ 0xA001;  // 右移并且异或多项式
            }
            else{
                crc >>= 1;  // 直接右移
            }
        }
    }
    return crc;
}

// 编码函数
std::vector<uint8_t> encode_tlv(const Tlvpacket& packet){
    std::vector<uint8_t> buffer;

    // 写入Header 2 1 2 1字节
    uint16_t magic_net = htons(packet.header.magic);
    buffer.push_back((magic_net >> 8) & 0xFF);      // 高字节
    buffer.push_back(magic_net & 0xFF);             // 低字节

    buffer.push_back(packet.header.version);

    uint16_t total_len_net = htons(packet.header.total_len);
    buffer.push_back((total_len_net >> 8) & 0xFF);
    buffer.push_back(total_len_net & 0xFF);

    buffer.push_back(packet.header.type);

    // 写入value(变长)
    buffer.insert(buffer.end(), packet.value.begin(), packet.value.end());

    // 计算并写入CRC
    uint16_t crc = crc16_modbus(buffer.data(), buffer.size());
    uint16_t crc_net = htons(crc);
    buffer.push_back((crc_net >> 8) & 0xFF);
    buffer.push_back(crc_net & 0xFF);

    // 返回结果
    return buffer;
}

// 解码函数
bool decode_tlv(const uint8_t* data, size_t length, Tlvpacket& packet){
    // 检查大小
    if (length < sizeof(TlvHeader) + sizeof(uint16_t)){
        return false;
    }
    // 读取Header
    uint16_t magic_net = (data[0] << 8) | data[1];
    packet.header.magic = ntohs(magic_net);

    // version
    packet.header.version = data[2];

    // total_len
    uint16_t total_len_net = (data[3] << 8) | data[4];
    packet.header.total_len = ntohs(total_len_net);

    // type
    packet.header.type = data[5];

    // 检查长度是否匹配
    if (length != packet.header.total_len){
        return false;
    }

    // 读取value
    size_t value_len = packet.header.total_len - sizeof(TlvHeader) - sizeof(uint16_t);
    packet.value.assign(data + sizeof(TlvHeader), data + sizeof(TlvHeader) + value_len);

    // 读取CRC
    uint16_t crc = (data[length - 2] << 8) | data[length - 1];
    packet.crc = ntohs(crc);

    // 校验CRC
    uint16_t calculated_crc = crc16_modbus(data, length - sizeof(uint16_t));
    if (calculated_crc != packet.crc){
        return false;
    }

    return true;
}
