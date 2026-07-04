#include "ModbusRtu.h"
#include <cstdint>
#include <vector>
#include "TlvProtocol.h"

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
    resp.byte_count = byte_count;
    resp.registers.resize(byte_count / 2);
    for (size_t i = 0; i < resp.registers.size(); i++) {
        resp.registers[i] = (data[3 + i * 2] << 8) | data[3 + i * 2 + 1];
    }
    return true;
}
