add_library(gateway_engine_lib STATIC
    pipeline/FramePool.cpp
    pipeline/stages/CaptureStage.cpp          
    pipeline/stages/PreprocessStage.cpp   
    pipeline/stages/InferenceStage.cpp   
    pipeline/stages/PostprocessStage.cpp   
    pipeline/Pipeline.cpp
    AnalysisScheduler.cpp
    HearbeatReporter.cpp
)

target_include_directories(gateway_engine_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway_access
)

# 链接 GStreamer 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)

target_include_directories(gateway_engine_lib PUBLIC ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(gateway_engine_lib PUBLIC common gateway_access_lib ${GSTREAMER_LIBRARIES})

add_executable(gateway_engine main.cpp)
target_link_libraries(gateway_engine PRIVATE gateway_engine_lib)

# 链接 RKNN 库
target_link_libraries(gateway_engine_lib PUBLIC rknnrt)
