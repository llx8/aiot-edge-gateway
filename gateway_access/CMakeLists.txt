# 网关核心逻辑库（不含 main，供测试链接）
add_library(gateway_access_lib STATIC
    UdsClient.cpp
)

target_include_directories(gateway_access_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(gateway_access_lib PUBLIC common)

# 可执行文件（thin wrapper）
add_executable(gateway_access main.cpp)
target_link_libraries(gateway_access PRIVATE gateway_access_lib)

# 独立驱动 .so（dlopen 插件）
add_library(modbus_rtu SHARED ModbusRtuDriver.cpp)
target_link_libraries(modbus_rtu PRIVATE common)
target_include_directories(modbus_rtu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_library(modbus_tcp SHARED ModbusTcpDriver.cpp)
target_link_libraries(modbus_tcp PRIVATE common)
target_include_directories(modbus_tcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)