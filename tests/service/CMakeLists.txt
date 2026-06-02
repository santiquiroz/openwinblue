add_executable(owb_service_tests
    smoke_test.cpp
    codec_sbc_test.cpp
    ipc_test.cpp
    hfp_guard_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    owb_codec_sbc
    owb_sbc
    owb_hfp_guard
    owb_ipc_server
    ole32 oleaut32
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
    ${CMAKE_SOURCE_DIR}/service/src
    ${CMAKE_SOURCE_DIR}/third-party/libsbc
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat
)

target_compile_options(owb_service_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
