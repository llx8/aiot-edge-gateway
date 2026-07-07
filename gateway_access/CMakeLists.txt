# 网关核心逻辑库（不含 main，供测试链接）
add_library(gateway_access_lib STATIC
    ModbusRtuDriver.cpp
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
