add_executable(owb_service_tests
    smoke_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
)

target_compile_options(owb_service_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
