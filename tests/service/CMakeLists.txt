add_executable(owb_service_tests
    smoke_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
