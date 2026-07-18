cmake_minimum_required(VERSION 3.15)
project(AIOT_EDGE_GATEWAY LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# W18: 兜底警告。原 HANDOVER 上的多条"返回值未检查"都是在 -Wall 默认级别下能扫出来的。
# 不开 -Werror 是因为现有代码还有若干 -Wunused-result（read/write 返回值）需要逐步清理。
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable)

add_subdirectory(common)
add_subdirectory(gateway_access)
add_subdirectory(gateway_core)
add_subdirectory(gateway_monitor)
add_subdirectory(gateway_engine)
add_subdirectory(gateway_watchdog)
enable_testing()
add_subdirectory(tests)