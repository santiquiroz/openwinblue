add_executable(owb_service_tests
    smoke_test.cpp
    codec_sbc_test.cpp
    codec_aptx_test.cpp
    codec_ldac_test.cpp
    ipc_test.cpp
    hfp_guard_test.cpp
    a2dp_stream_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    owb_codec_sbc
    owb_sbc
    owb_codec_aptx
    owb_aptx
    owb_codec_ldac
    owb_ldac
    owb_hfp_guard
    owb_ipc_server
    owb_a2dp_stream
    ole32 oleaut32
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
    ${CMAKE_SOURCE_DIR}/service/src
    ${CMAKE_SOURCE_DIR}/third-party/libsbc
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat
    ${CMAKE_SOURCE_DIR}/third-party/libldac/inc
    ${CMAKE_SOURCE_DIR}/third-party/libldac-compat
    ${CMAKE_SOURCE_DIR}/driver
)

target_compile_options(owb_service_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
