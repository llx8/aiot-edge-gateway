cmake_minimum_required(VERSION 3.15)
project(AIOT_EDGE_GATEWAY LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(common)
add_subdirectory(gateway_access)
add_subdirectory(gateway_core)
add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)