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
    ${CMAKE_CURRENT_SOURCE_DIR}               # stub rnnoise_data.h (CI: not in submodule)
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

# ── ONNX Runtime detection (optional — enables DeepFilterNet3) ────────────────
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS
        "$ENV{ONNXRUNTIME_ROOT}/include"
        "C:/Program Files/onnxruntime/include"
        "C:/onnxruntime/include"
)
find_library(ONNXRUNTIME_LIB onnxruntime
    HINTS
        "$ENV{ONNXRUNTIME_ROOT}/lib"
        "C:/Program Files/onnxruntime/lib"
        "C:/onnxruntime/lib"
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIB}")
    set(OWB_HAVE_ONNXRUNTIME 1)
else()
    message(STATUS "ONNX Runtime not found — DeepFilterNet3 will be a passthrough stub")
    set(OWB_HAVE_ONNXRUNTIME 0)
endif()

# ── AI Pipeline (NoiseReducer + DeepFilter + AiPipeline) ──────────────────────
add_library(owb_ai_pipeline STATIC
    noise_reducer.cpp
    deep_filter.cpp
    ai_pipeline.cpp
)
target_include_directories(owb_ai_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(owb_ai_pipeline PUBLIC
    OWB_HAVE_ONNXRUNTIME=${OWB_HAVE_ONNXRUNTIME}
)
if(OWB_HAVE_ONNXRUNTIME)
    target_include_directories(owb_ai_pipeline PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(owb_ai_pipeline PUBLIC owb_rnnoise ${ONNXRUNTIME_LIB})
else()
    target_link_libraries(owb_ai_pipeline PUBLIC owb_rnnoise)
endif()
target_compile_options(owb_ai_pipeline PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
