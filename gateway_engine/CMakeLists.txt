add_library(gateway_engine_lib STATIC
    placeholder.cpp
)

target_include_directories(gateway_engine_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(gateway_engine_lib PUBLIC common)

add_executable(gateway_engine main.cpp)
target_link_libraries(gateway_engine PRIVATE gateway_engine_lib)
