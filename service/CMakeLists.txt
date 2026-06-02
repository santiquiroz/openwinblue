# ── libsbc static library (BlueZ SBC codec, LGPL 2.1) ───────────────────────
add_library(owb_sbc STATIC
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc/sbc.c
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc/sbc_primitives.c
)
target_include_directories(owb_sbc PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/libsbc          # for <sbc/sbc.h>
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc      # for sbc_private.h etc.
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat   # for sbc_compat.h
)
target_compile_options(owb_sbc PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
)
# MSVC: force-include sbc_compat.h before every sbc source file
target_compile_options(owb_sbc PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/FIsbc_compat.h>
)

# ── SBC codec C++ wrapper (ICodec implementation) ────────────────────────────
add_library(owb_codec_sbc STATIC
    codecs/codec_sbc.cpp
)
target_include_directories(owb_codec_sbc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
)
target_link_libraries(owb_codec_sbc PUBLIC owb_sbc)
target_compile_options(owb_codec_sbc PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8 /FIsbc_compat.h>
)

# ── owb_service executable ───────────────────────────────────────────────────
add_executable(owb_service
    src/main.cpp
    src/audio_capture.cpp
    src/hfp_guard.cpp
    src/ipc_server.cpp
)
target_include_directories(owb_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
)
target_link_libraries(owb_service PRIVATE owb_codec_sbc owb_sbc)
target_compile_options(owb_service PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
target_link_libraries(owb_service PRIVATE
    ole32 oleaut32 uuid Mmdevapi audiopolicy
)

add_subdirectory(ai)
