#include "ModbusRtu.h"
#include <cstdint>
#include <vector>

// 功能码
#define MODBUS_FUNCTION_CODE 0x03

// 计算请求长度，包含crc校验
static const uint8_t request_len = sizeof(uint8_t) * 2 + sizeof(uint16_t) * 2 + sizeof(uint16_t);

// 编码实现
std::vector<uint8_t> encode_request(const ModbusRequest& req){
    // 定义请求缓冲区，包含crc校验
    std::vector<uint8_t> buf(request_len);
    // 填充数据，单字节直接写，双字节需要分两次写
    buf[0] = req.slave_addr;
    buf[1] = MODBUS_FUNCTION_CODE; // 功能码
    buf[2] = req.start_addr >> 8;
    buf[3] = req.start_addr & 0xFF;
    buf[4] = req.quantity >> 8;
    buf[5] = req.quantity & 0xFF;
    // 加入crc校验
    uint16_t crc = crc16_modbus(buf.data(), request_len - sizeof(uint16_t));
    // modbus里面crc是低字节在前，高字节在后
    buf[6] = crc & 0xFF;
    buf[7] = crc >> 8;
    return buf;
}

// 解码实现
bool decode_response(const uint8_t* data, size_t len, ModbusResponse& resp){
    if (len < 6) {
        return false;
    }
    // 校验crc
    uint16_t crc = crc16_modbus(data, len - sizeof(uint16_t));
    uint16_t crc_wire = data[len - 2] |(static_cast<uint16_t>(data[len - 1]) << 8);
    if (crc != crc_wire) {
        return false;
    }
    // 解析数据
    resp.slave_addr = data[0];
    resp.function_code = data[1];
    // 检查功能码
    if (resp.function_code & 0x80) {
        return false;   // 错误码
    }
    uint8_t byte_count = data[2];
    // 越界保护：byte_count 来自总线，须校验数据区 + CRC 不超过实际长度
    if (3 + byte_count + 2 > len) {
        return false;
    }
    resp.byte_count = byte_count;
    resp.registers.resize(byte_count / 2);
    for (size_t i = 0; i < resp.registers.size(); i++) {
        resp.registers[i] = (static_cast<uint16_t>(data[3 + i * 2]) << 8) | data[3 + i * 2 + 1];
    }
    return true;
}

// 计算crc16
// CRC16-Modbus 查表法
uint16_t crc16_modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// Modbus TCP 请求编码
std::vector<uint8_t> encode_tcp_request(uint16_t trans_id, const ModbusRequest& req) {
    // TCP 帧 = 7字节 MBAP头 + 5字节 PDU（无CRC）= 12字节
    std::vector<uint8_t> result(12);
    // MBAP 头
    result[0] = trans_id >> 8;       // 事务ID 高
    result[1] = trans_id & 0xFF;     // 事务ID 低
    result[2] = 0x00;                // 协议ID 高
    result[3] = 0x00;                // 协议ID 低
    result[4] = 0x00;                // 长度 高
    result[5] = 0x06;                // 长度 = 单元ID(1) + 功能码(1) + 地址(2) + 数量(2) = 6
    // MBAP 单元ID + PDU
    result[6] = req.slave_addr;      // 单元ID
    result[7] = req.function_code;   // 功能码
    result[8] = req.start_addr >> 8;
    result[9] = req.start_addr & 0xFF;
    result[10] = req.quantity >> 8;
    result[11] = req.quantity & 0xFF;
    return result;
}

// Modbus TCP 响应解码
bool decode_tcp_response(const uint8_t* data, size_t len, ModbusResponse& resp){
    if (len < 9) {
        return false;
    }
    // 逐字节解析数据
    resp.slave_addr = data[6];
    resp.function_code = data[7];
    // 检查功能码
    if (resp.function_code & 0x80) {
        return false;   // 错误码
    }
    uint8_t byte_count = data[8];
    // TCP 无 CRC 保护，byte_count 越界校验必不可少（否则可越界读栈内存）
    if (9 + byte_count > len) {
        return false;
    }
    resp.byte_count = byte_count;
    resp.registers.resize(byte_count / 2);
    for (size_t i = 0; i < resp.registers.size(); i++) {
        resp.registers[i] = (static_cast<uint16_t>(data[9 + i * 2]) << 8) | data[9 + i * 2 + 1];
    }
    return true;
}
