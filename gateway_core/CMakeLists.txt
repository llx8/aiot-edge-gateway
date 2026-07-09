find_package(SQLite3 REQUIRED)
find_package(eclipse-paho-mqtt-c REQUIRED)

add_library(gateway_core_lib STATIC
    EventLoop.cpp
    SqliteStore.cpp
    ShmPublisher.cpp
    DbWriter.cpp
    MqttClient.cpp
    RuleEngine.cpp
    OfflineStore.cpp
    RpcHandler.cpp
)

target_include_directories(gateway_core_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(gateway_core_lib PUBLIC common SQLite::SQLite3 eclipse-paho-mqtt-c::paho-mqtt3a paho-mqttpp3)

# 可执行文件（thin wrapper） — main.cpp 第4步才写
add_executable(gateway_core main.cpp)
target_link_libraries(gateway_core PRIVATE gateway_core_lib)