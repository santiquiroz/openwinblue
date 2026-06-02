# The KMDF driver (owb_a2dp.sys) is built via the WDK Visual Studio integration.
# Open driver/owb_a2dp.vcxproj in Visual Studio 2022 with WDK 11 installed.
#
# Build from command line:
#   msbuild driver\owb_a2dp.vcxproj /p:Configuration=Debug /p:Platform=x64
#
# The .vcxproj is generated via: File > New Project > Kernel Mode Driver (KMDF) in VS.
# After generating, replace the generated DriverEntry with driver/src/owb_a2dp.c.
