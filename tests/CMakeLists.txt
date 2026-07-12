# 使用系统已安装的 gtest（apt install libgtest-dev）
find_package(GTest REQUIRED QUIET)
if(NOT GTest_FOUND)
    # 回退：从 /usr/src 源码构建
    message(STATUS "gtest not found via find_package; building from /usr/src...")
    add_subdirectory(/usr/src/googletest "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
endif()

add_executable(test_RingBuffer test_RingBuffer.cpp)
target_link_libraries(test_RingBuffer common GTest::gtest_main)

add_executable(test_ShmLayout test_ShmLayout.cpp)
target_link_libraries(test_ShmLayout common GTest::gtest_main)

add_test(NAME RingBufferTest  COMMAND test_RingBuffer)
add_test(NAME ShmLayoutTest   COMMAND test_ShmLayout)
add_executable(test_InternalMessage test_InternalMessage.cpp)
target_link_libraries(test_InternalMessage common GTest::gtest_main)

add_test(NAME InternalMessageTest COMMAND test_InternalMessage)

add_executable(test_SqliteStore test_SqliteStore.cpp)
target_include_directories(test_SqliteStore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_core)
target_link_libraries(test_SqliteStore gateway_core_lib GTest::gtest_main)

add_executable(test_EventLoop test_EventLoop.cpp)
target_include_directories(test_EventLoop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_core)
target_link_libraries(test_EventLoop gateway_core_lib GTest::gtest_main pthread)

add_executable(test_ShmPublisher test_ShmPublisher.cpp)
target_include_directories(test_ShmPublisher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_core)
target_link_libraries(test_ShmPublisher gateway_core_lib GTest::gtest_main)

add_test(NAME SqliteStoreTest COMMAND test_SqliteStore)
add_test(NAME EventLoopTest  COMMAND test_EventLoop)
add_test(NAME ShmPublisherTest COMMAND test_ShmPublisher)

add_executable(test_ShmReader test_ShmReader.cpp)
target_link_libraries(test_ShmReader gateway_monitor_lib gateway_core_lib GTest::gtest_main)

add_test(NAME ShmReaderTest COMMAND test_ShmReader)

add_executable(test_ModbusRtu test_ModbusRtu.cpp)
target_link_libraries(test_ModbusRtu common GTest::gtest_main)

add_test(NAME ModbusRtuTest COMMAND test_ModbusRtu)

add_executable(test_SpscQueue test_SpscQueue.cpp)
target_link_libraries(test_SpscQueue common GTest::gtest_main)
add_test(NAME SpscQueueTest COMMAND test_SpscQueue)

add_executable(test_ModbusTcp test_ModbusTcp.cpp)
target_link_libraries(test_ModbusTcp common GTest::gtest_main)
add_test(NAME ModbusTcpTest COMMAND test_ModbusTcp)

add_executable(test_RuleEngine test_RuleEngine.cpp)
target_include_directories(test_RuleEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_core)
target_link_libraries(test_RuleEngine gateway_core_lib GTest::gtest_main)
add_test(NAME RuleEngineTest COMMAND test_RuleEngine)

add_executable(test_PipelineFps test_PipelineFps.cpp)
target_include_directories(test_PipelineFps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_engine)
target_link_libraries(test_PipelineFps gateway_engine_lib GTest::gtest_main)
add_test(NAME PipelineFpsTest COMMAND test_PipelineFps)

add_executable(test_EventFusion test_EventFusion.cpp)
target_include_directories(test_EventFusion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_core)
target_link_libraries(test_EventFusion gateway_core_lib GTest::gtest_main)
add_test(NAME EventFusionTest COMMAND test_EventFusion)
