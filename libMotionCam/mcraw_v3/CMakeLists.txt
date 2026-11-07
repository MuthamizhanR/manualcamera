cmake_minimum_required(VERSION 3.10)
project(mcraw_v3)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/queue/queue
)

add_library(mcraw_v3
    Encoder.cpp
    WriterQueue.cpp
)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    target_compile_options(mcraw_v3 PRIVATE -O3)
else()
    target_compile_options(mcraw_v3 PRIVATE -O3)
endif()

enable_testing()
add_executable(mcraw_v3_test test.cpp)
target_link_libraries(mcraw_v3_test mcraw_v3)
add_test(NAME mcraw_v3_test COMMAND mcraw_v3_test)
