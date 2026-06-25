find_package(spdlog REQUIRED)

add_library(common STATIC
    TlvProtocol.cpp
    RingBuffer.cpp
    Logger.cpp
    Protocol.cpp
    InternalMessage.cpp
    Config.cpp
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC spdlog::spdlog)
