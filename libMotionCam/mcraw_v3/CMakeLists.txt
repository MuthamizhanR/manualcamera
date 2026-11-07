cmake_minimum_required(VERSION 3.10)
project(mcraw_v3_encoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder/thirdparty
)

add_library(mcraw_v3_encoder Encoder.cpp)
set_property(TARGET mcraw_v3_encoder PROPERTY POSITION_INDEPENDENT_CODE ON)

if (BUILD_TESTING)
    # Build reference decoder to link with
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../reference/motioncam-decoder ${CMAKE_CURRENT_BINARY_DIR}/decoder_build)

    add_executable(mcraw_v3_test tests/EncoderTest.cpp)
    target_link_libraries(mcraw_v3_test PRIVATE mcraw_v3_encoder motioncam_decoder)

    add_test(NAME mcraw_v3_test COMMAND mcraw_v3_test)
endif()
