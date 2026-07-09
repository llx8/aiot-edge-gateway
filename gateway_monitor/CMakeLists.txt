# 找Qt5库
find_package(Qt5 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

# 源文件列表
set(SOURCES
    ShmReader.cpp
    MonitorWindow.cpp
    DashboardWidget.cpp
    SensorChartWidget.cpp
    AlarmTableWidget.cpp
)

# 头文件列表
set(HEADERS
    ShmReader.h
    MonitorWindow.h
    DashboardWidget.h
    SensorChartWidget.h
    AlarmTableWidget.h
)

# 先构建库（测试需要链接，后续 UI 文件加入后再构建可执行文件）
add_library(gateway_monitor_lib ${SOURCES} ${HEADERS})
target_include_directories(gateway_monitor_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_link_libraries(gateway_monitor_lib PUBLIC Qt5::Widgets common)

# 构建可执行文件
add_executable(gateway_monitor main.cpp)
target_link_libraries(gateway_monitor PRIVATE gateway_monitor_lib)