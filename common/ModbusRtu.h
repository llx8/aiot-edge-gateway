#pragma once

#include <cstdint>
#include <vector>

// Modbus请求结构体
struct ModbusRequest {
    uint8_t slave_addr;         // 从站地址
    uint8_t function_code;      // 功能码
    uint16_t start_addr;        // 起始地址
    uint16_t quantity;          // 读取数量
};

// Modbus响应结构体 
struct ModbusResponse {
    uint8_t slave_addr;         // 从站地址
    uint8_t function_code;      // 功能码
    uint16_t byte_count;        // 数据字节数
    std::vector<uint16_t> registers;  // 寄存器值
};

// 编码
std::vector<uint8_t> encode_request(const ModbusRequest& req);

// 解码
bool decode_response(const uint8_t* data, size_t len, ModbusResponse& resp);

// 计算crc16
uint16_t crc16_modbus(const uint8_t* data, size_t len);

// Modbus TCP 请求编码
std::vector<uint8_t> encode_tcp_request(uint16_t trans_id, const ModbusRequest& req);
// Modbus TCP 响应解码
bool decode_tcp_response(const uint8_t* data, size_t len, ModbusResponse& resp);