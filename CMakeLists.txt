cmake_minimum_required(VERSION 3.26)
project(SpaceCadetPinball VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

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
find_package(box2d 3.2 REQUIRED CONFIG)

# Platform-independent game logic. No SDL dependency so it is unit-testable headless.
add_library(pinball_core STATIC
  src/core/Args.cpp
  src/core/AssetManifest.cpp
  src/core/AssetName.cpp
  src/core/Format.cpp
  src/core/Game.cpp
  src/core/Missions.cpp
  src/core/Physics.cpp
  src/core/PngHeader.cpp
  src/core/Rules.cpp
  src/core/Sha256.cpp
  src/core/TableLayout.cpp
)
target_include_directories(pinball_core PUBLIC src)
target_link_libraries(pinball_core PUBLIC box2d::box2d)
# PUBLIC so the same warnings, hardening and sanitizer flags reach every consumer.
pinball_apply_hardening(pinball_core PUBLIC)

# SDL-facing code shared by the game and the snapshot tool.
add_library(pinball_platform STATIC
  src/platform/App.cpp
  src/platform/Assets.cpp
  src/platform/EmbeddedManifest.cpp
  src/platform/Hud.cpp
  src/platform/TableRenderer.cpp
  src/platform/Text.cpp
)
target_link_libraries(pinball_platform PUBLIC pinball_core SDL3::SDL3)

add_executable(pinball src/main.cpp)
target_link_libraries(pinball PRIVATE pinball_platform)

# Asset integrity: the size and SHA-256 of every asset are compiled into the
# executable, which checks the files against them before it starts. The
# manifest is regenerated on every build and rewritten only when it changes.
set(PINBALL_MANIFEST_DIR ${CMAKE_BINARY_DIR}/generated)
set(PINBALL_MANIFEST ${PINBALL_MANIFEST_DIR}/AssetManifest.inc)
file(MAKE_DIRECTORY ${PINBALL_MANIFEST_DIR})
add_custom_target(pinball_manifest
  COMMAND ${CMAKE_COMMAND} -DASSETS_DIR=${CMAKE_SOURCE_DIR}/assets -DOUTPUT=${PINBALL_MANIFEST}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenerateAssetManifest.cmake
  BYPRODUCTS ${PINBALL_MANIFEST}
  COMMENT "Hashing assets"
  VERBATIM)
add_dependencies(pinball_platform pinball_manifest)
target_include_directories(pinball_platform PRIVATE ${PINBALL_MANIFEST_DIR})
set_source_files_properties(src/platform/EmbeddedManifest.cpp PROPERTIES
  OBJECT_DEPENDS ${PINBALL_MANIFEST})

# The game loads assets from an "assets" directory next to the executable.
# Copied by a target that runs on every build (not only on relink), so an
# edited asset is never stale. Nothing is ever deleted: a removed asset leaves
# an unused file behind in the build tree, which is harmless.
add_custom_target(pinball_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:pinball>/assets
  COMMENT "Copying assets"
  VERBATIM)
# $<TARGET_FILE_DIR> adds no dependency, so this is not circular. It makes
# building just the pinball target bring its assets along.
add_dependencies(pinball pinball_assets)

# Development tool that renders a frame offscreen to a PNG. Not part of the
# game; built with the tests.
option(PINBALL_BUILD_TOOLS "Build development tools" ON)
if(PINBALL_BUILD_TOOLS)
  add_executable(pinball_snapshot tools/snapshot/main.cpp)
  target_link_libraries(pinball_snapshot PRIVATE pinball_platform)
  # It reads the same assets directory, next to the game.
  set_target_properties(pinball_snapshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:pinball>)
  add_dependencies(pinball_snapshot pinball_assets)

  # Prints the table geometry as JSON for the art scripts.
  add_executable(pinball_layout tools/layout_dump/main.cpp)
  target_link_libraries(pinball_layout PRIVATE pinball_core)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(core_tests
    tests/main.cpp
    tests/ArgsTest.cpp
    tests/AssetNameTest.cpp
    tests/BumperTest.cpp
    tests/FixedTimestepTest.cpp
    tests/FlipperTest.cpp
    tests/FormatTest.cpp
    tests/GameTest.cpp
    tests/HoleTest.cpp
    tests/MissionTest.cpp
    tests/NudgeTest.cpp
    tests/PhysicsTest.cpp
    tests/PlungerTest.cpp
    tests/PngHeaderTest.cpp
    tests/RulesTest.cpp
    tests/Sha256Test.cpp
    tests/TargetTest.cpp
  )
  target_link_libraries(core_tests PRIVATE pinball_core)
  add_test(NAME core_tests COMMAND core_tests)

  # Assets must be well-formed PNGs with no metadata.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME assets_clean
      COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/check_assets.py
              ${CMAKE_SOURCE_DIR}/assets)
  endif()

  # The built game must refuse to start if any asset is altered.
  if(Python3_Interpreter_FOUND)
    add_test(NAME asset_integrity
      COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/test_asset_integrity.py
              $<TARGET_FILE:pinball> ${CMAKE_BINARY_DIR}/asset_integrity_scratch)
    set_tests_properties(asset_integrity PROPERTIES TIMEOUT 120)
  endif()

  # Rendering smoke test: play ten seconds headless and draw a frame. The
  # tool fails if the frame is blank.
  if(PINBALL_BUILD_TOOLS)
    add_test(NAME render_snapshot
      COMMAND pinball_snapshot 1200 render_snapshot.png
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(render_snapshot PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 60)
  endif()

  # The geometry dump must be valid JSON.
  if(PINBALL_BUILD_TOOLS AND Python3_Interpreter_FOUND)
    add_test(NAME layout_json
      COMMAND Python3::Interpreter -c
              "import json,subprocess,sys; d=json.loads(subprocess.check_output([sys.argv[1]])); assert len(d['shell'])>10 and d['bumpers'] and d['holes']"
              $<TARGET_FILE:pinball_layout>)
  endif()

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
