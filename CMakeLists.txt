cmake_minimum_required(VERSION 3.28)
project(openwinblue VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest via FetchContent (used in tests/)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_subdirectory(service)
add_subdirectory(tests/service)

# The driver itself is built separately via WDK (driver/owb_a2dp.vcxproj);
# driver/CMakeLists.txt only builds its WDK-free AVDTP packet module for host tests.
add_subdirectory(driver)
add_subdirectory(tests/driver)
