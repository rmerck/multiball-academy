cmake_minimum_required(VERSION 3.24)
project(SpaceCadetPinball VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PINBALL_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(PINBALL_WERROR "Treat warnings as errors" OFF)

include(cmake/Hardening.cmake)

find_package(SDL3 REQUIRED CONFIG)

# Platform-independent logic. No SDL dependency so it is unit-testable headless.
add_library(pinball_core INTERFACE)
target_include_directories(pinball_core INTERFACE src)
pinball_apply_hardening(pinball_core INTERFACE)

add_executable(pinball
  src/main.cpp
  src/platform/App.cpp
)
target_link_libraries(pinball PRIVATE pinball_core SDL3::SDL3)
pinball_apply_hardening(pinball PRIVATE)

include(CTest)
if(BUILD_TESTING)
  add_executable(core_tests tests/FixedTimestepTest.cpp)
  target_link_libraries(core_tests PRIVATE pinball_core)
  add_test(NAME core_tests COMMAND core_tests)

  # Headless smoke test: run the real binary for a few frames with no display.
  add_test(NAME smoke_headless COMMAND pinball --frames 120)
  set_tests_properties(smoke_headless PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
    TIMEOUT 30)
  add_test(NAME rejects_bad_args COMMAND pinball --frames -5)
  set_tests_properties(rejects_bad_args PROPERTIES WILL_FAIL TRUE)
endif()
