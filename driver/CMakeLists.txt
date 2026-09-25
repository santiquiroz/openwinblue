# The KMDF driver (owb_a2dp.sys) is built via the WDK Visual Studio integration.
# Open driver/owb_a2dp.vcxproj in Visual Studio 2022 with WDK 11 installed.
#
# Build from command line:
#   msbuild driver\owb_a2dp.vcxproj /p:Configuration=Debug /p:Platform=x64
#
# The .vcxproj is generated via: File > New Project > Kernel Mode Driver (KMDF) in VS.
# After generating, replace the generated DriverEntry with driver/src/owb_a2dp.c.
#
# Only the WDK-free AVDTP packet module is built here, so host tests can link it.

add_library(owb_avdtp_packets STATIC src/avdtp_packets.c)

target_include_directories(owb_avdtp_packets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(owb_avdtp_packets PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
