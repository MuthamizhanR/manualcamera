cmake_minimum_required(VERSION 3.10)
project(mcraw_v3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/queue/queue
    ${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder/thirdparty
)

add_library(mcraw_v3_encoder
    Encoder.cpp
    WriterQueue.cpp
)
set_property(TARGET mcraw_v3_encoder PROPERTY POSITION_INDEPENDENT_CODE ON)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    target_compile_options(mcraw_v3_encoder PRIVATE -O3)
else()
    target_compile_options(mcraw_v3_encoder PRIVATE -O3)
endif()

add_library(mcraw_v3 ALIAS mcraw_v3_encoder)

if (BUILD_TESTING)
    # Build reference decoder to link with
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder ${CMAKE_CURRENT_BINARY_DIR}/decoder_build)

    add_executable(mcraw_v3_test tests/EncoderTest.cpp)
    target_link_libraries(mcraw_v3_test PRIVATE mcraw_v3_encoder motioncam_decoder)
    add_test(NAME mcraw_v3_test COMMAND mcraw_v3_test)

    add_executable(mcraw_v3_opt_test test.cpp)
    target_link_libraries(mcraw_v3_opt_test PRIVATE mcraw_v3_encoder)
    add_test(NAME mcraw_v3_opt_test COMMAND mcraw_v3_opt_test)
endif()
