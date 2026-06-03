# ── RNNoise static library (Xiph.Org, BSD 3-Clause) ──────────────────────────
# Core sources always compiled:
set(OWB_RNNOISE_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/denoise.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/kiss_fft.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/nnet.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/nnet_default.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/celt_lpc.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnn.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnnoise_tables.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/pitch.c
)

# Model data: use downloaded weights if available, else passthrough stub.
if(EXISTS "${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnnoise_data.c")
    set(OWB_RNNOISE_MODEL_SOURCES
        ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnnoise_data.c
        ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/parse_lpcnet_weights.c
    )
    set(OWB_RNNOISE_HAVE_MODEL 1)
else()
    # rnnoise_model_stub.c provides rnnoise_arrays/init_rnnoise/rnn_parse_weights
    # as no-ops; rnnoise_create() returns NULL and process() passes audio through.
    set(OWB_RNNOISE_MODEL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/rnnoise_model_stub.c
    )
    set(OWB_RNNOISE_HAVE_MODEL 0)
endif()

add_library(owb_rnnoise STATIC
    ${OWB_RNNOISE_CORE_SOURCES}
    ${OWB_RNNOISE_MODEL_SOURCES}
)
target_include_directories(owb_rnnoise PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/include
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src
)
target_compile_options(owb_rnnoise PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
)
target_compile_definitions(owb_rnnoise PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:restrict=>
)
target_compile_definitions(owb_rnnoise PUBLIC
    OWB_RNNOISE_HAVE_MODEL=${OWB_RNNOISE_HAVE_MODEL}
)

# ── AI Pipeline (NoiseReducer + AiPipeline) ───────────────────────────────────
add_library(owb_ai_pipeline STATIC
    noise_reducer.cpp
    ai_pipeline.cpp
)
target_include_directories(owb_ai_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(owb_ai_pipeline PUBLIC owb_rnnoise)
target_compile_options(owb_ai_pipeline PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
