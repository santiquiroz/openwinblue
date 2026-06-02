add_executable(owb_service
    src/main.cpp
)

target_include_directories(owb_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
)

target_compile_options(owb_service PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

add_subdirectory(ai)
