add_executable(mock_sensor mock_sensor.cpp)

target_include_directories(mock_sensor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

find_package(Threads REQUIRED)
target_link_libraries(mock_sensor PRIVATE common Threads::Threads)
