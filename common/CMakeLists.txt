find_package(spdlog REQUIRED)

add_library(common STATIC
    RingBuffer.cpp
    Logger.cpp
    InternalMessage.cpp
    Config.cpp
    ModbusRtu.cpp
    DriverLoader.cpp
    GpioDriver.cpp
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC spdlog::spdlog ${CMAKE_DL_LIBS})

set_property(TARGET common PROPERTY POSITION_INDEPENDENT_CODE ON)