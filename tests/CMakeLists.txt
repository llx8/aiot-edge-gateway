include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

add_executable(test_TlvProtocol test_TlvProtocol.cpp)
target_link_libraries(test_TlvProtocol common GTest::gtest_main)

add_executable(test_RingBuffer test_RingBuffer.cpp)
target_link_libraries(test_RingBuffer common GTest::gtest_main)

add_executable(test_ShmLayout test_ShmLayout.cpp)
target_link_libraries(test_ShmLayout common GTest::gtest_main)

add_executable(test_Protocol test_Protocol.cpp)
target_link_libraries(test_Protocol common GTest::gtest_main)

add_executable(test_Session test_Session.cpp)
target_include_directories(test_Session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_access)
target_link_libraries(test_Session common gateway_access_lib GTest::gtest_main)

add_test(NAME TlvProtocolTest COMMAND test_TlvProtocol)
add_test(NAME RingBufferTest  COMMAND test_RingBuffer)
add_test(NAME ShmLayoutTest   COMMAND test_ShmLayout)
add_test(NAME ProtocolTest    COMMAND test_Protocol)
add_executable(test_InternalMessage test_InternalMessage.cpp)
target_link_libraries(test_InternalMessage common GTest::gtest_main)

add_test(NAME InternalMessageTest COMMAND test_InternalMessage)
add_test(NAME SessionTest     COMMAND test_Session)
