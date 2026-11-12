cmake_minimum_required(VERSION 3.24)
project(SpaceCadetPinball VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PINBALL_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(PINBALL_WERROR "Treat warnings as errors" OFF)

include(cmake/Hardening.cmake)

# 3.4 is the first release with the built-in PNG loader used for assets.
find_package(SDL3 3.4 REQUIRED CONFIG)

# Platform-independent game logic. No SDL dependency so it is unit-testable headless.
add_library(pinball_core STATIC
  src/core/Args.cpp
  src/core/Game.cpp
)
target_include_directories(pinball_core PUBLIC src)
# PUBLIC so the same warnings, hardening and sanitizer flags reach every consumer.
pinball_apply_hardening(pinball_core PUBLIC)

add_executable(pinball
  src/main.cpp
  src/platform/App.cpp
)
target_link_libraries(pinball PRIVATE pinball_core SDL3::SDL3)

include(CTest)
if(BUILD_TESTING)
  add_executable(core_tests
    tests/main.cpp
    tests/ArgsTest.cpp
    tests/FixedTimestepTest.cpp
    tests/GameTest.cpp
  )
  target_link_libraries(core_tests PRIVATE pinball_core)
  add_test(NAME core_tests COMMAND core_tests)

  set(PINBALL_HEADLESS_ENV "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

  # Headless smoke test: run the real binary with no display. The binary itself
  # fails if no simulation step ran.
  add_test(NAME smoke_headless COMMAND pinball --frames 120)
  set_tests_properties(smoke_headless PROPERTIES
    ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 30)

  # Bad arguments must be rejected cleanly: exit code 1, usage text, and no
  # sanitizer report. Checked by a script because CTest's WILL_FAIL and
  # PASS_REGULAR_EXPRESSION cannot assert all three together.
  set(bad_args_cases
    "negative:--frames|-5"
    "zero:--frames|0"
    "too_large:--frames|1000001"
    "trailing_junk:--frames|5x"
    "missing_value:--frames"
    "unknown_flag:--bogus")
  foreach(case IN LISTS bad_args_cases)
    string(REGEX MATCH "^[^:]+" name "${case}")
    string(REGEX REPLACE "^[^:]+:" "" args "${case}")
    add_test(NAME rejects_${name}
      COMMAND ${CMAKE_COMMAND} -DPINBALL=$<TARGET_FILE:pinball> -DARGS=${args}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ExpectUsageError.cmake)
    set_tests_properties(rejects_${name} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
