add_executable(owb_driver_tests
    avdtp_packets_test.cpp
)

target_link_libraries(owb_driver_tests PRIVATE
    GTest::gtest_main
    owb_avdtp_packets
)

target_compile_options(owb_driver_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_driver_tests)
