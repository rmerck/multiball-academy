cmake_minimum_required(VERSION 3.26)

# The oldest macOS a self-contained build (PINBALL_FETCH_DEPS) runs on, and
# the LSMinimumSystemVersion of its application bundle. 11.0 is the first
# release for Apple silicon; SDL 3.4 itself asks for 10.13, and the C++ the
# game uses for nothing newer. It has to be set before project(), which reads
# it, so the option is read here from the cache, where -D put it. A value
# given on the command line or in the environment wins. A build against
# installed libraries is left to target the macOS it is built on, as those
# libraries do. Other systems ignore all of this.
# SPIKE: on iOS the macOS default (11.0) must not apply; the iOS minimum is
# its own number.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if("${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum iOS version" FORCE)
  endif()
elseif(CMAKE_HOST_APPLE AND PINBALL_FETCH_DEPS AND "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL ""
   AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(SpaceCadetPinball VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

# SPIKE: one switch for "this is the iOS target". APPLE is true on iOS too,
# so every if(APPLE) below that means macOS has to be told apart.
set(PINBALL_IOS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(PINBALL_IOS ON)
endif()

if(APPLE)
  # For the menu bar hook (src/platform/MacMenu.mm). Enabled here, before any
  # dependency is added, so every directory of the build knows the language.
  enable_language(OBJCXX)
endif()

# C++23 for every target, set before any target exists: the game, the tests,
# the tools and the fuzzer. (The pinned dependencies are C and Objective-C,
# which this does not touch. With MSVC, CMake passes /std:c++latest until
# it knows a C++23 flag for that compiler.)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
  # The same for the Objective-C++ file. CMake knows no C++23 flag for
  # Objective-C++ with Apple clang (up to 4.1 at least) and would stop with
  # "requires the language dialect OBJCXX23", so name the flag here. It is
  # the one CMake uses for C++ with the same compiler.
  if(CMAKE_OBJCXX_COMPILER_ID MATCHES "Clang"
     AND NOT DEFINED CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION)
    set(CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION "-std=c++2b")
    set(CMAKE_OBJCXX23_EXTENSION_COMPILE_OPTION "-std=gnu++2b")
  endif()
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Makes executables position independent at link time too, where supported.
include(CheckPIESupported)
check_pie_supported(LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PINBALL_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(PINBALL_WERROR "Treat warnings as errors" OFF)
option(PINBALL_FETCH_DEPS "Build pinned SDL3 and Box2D from source and link them statically" OFF)
option(PINBALL_FUZZ "Build only the parser fuzzer (clang with libFuzzer)" OFF)
option(PINBALL_REQUIRE_ALL_TESTS
  "Stop the configuration if a test cannot be registered (no Python 3, or the tools left out)"
  OFF)

include(cmake/Hardening.cmake)

# The version and the application identifier are written once, here and in
# project() above. The game reads them from this generated header
# (src/core/Version.hpp includes it) and the macOS bundle's Info.plist is
# filled in from the same values, so they cannot drift apart.
set(PINBALL_BUNDLE_ID "io.github.rmerck.spacecadetpinball")
if(PINBALL_IOS)
  # SPIKE: working title and identifier, in one place.
  set(PINBALL_BUNDLE_ID "io.github.rmerck.pinball.spike")
  set(PINBALL_IOS_NAME "Pinball")
endif()
set(PINBALL_VERSION_DIR ${CMAKE_BINARY_DIR}/generated_version)
configure_file(cmake/VersionConfig.hpp.in ${PINBALL_VERSION_DIR}/VersionConfig.hpp @ONLY)

if(PINBALL_FUZZ)
  # The fuzzer exercises the core parsers alone: no SDL, no window.
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PINBALL_FUZZ needs clang with libFuzzer (not Apple clang)")
  endif()
  if(NOT PINBALL_SANITIZE)
    message(WARNING "PINBALL_FUZZ without PINBALL_SANITIZE finds far less: add -DPINBALL_SANITIZE=ON")
  endif()
  add_library(pinball_fuzz_core STATIC
    src/core/Args.cpp
    src/core/AssetName.cpp
    src/core/HighScores.cpp
    src/core/PngHeader.cpp)
  target_include_directories(pinball_fuzz_core PUBLIC src)
  pinball_apply_hardening(pinball_fuzz_core PUBLIC)
  target_compile_options(pinball_fuzz_core PUBLIC -fsanitize=fuzzer-no-link)
  add_executable(pinball_fuzz tests/fuzz/FuzzParsers.cpp)
  target_link_libraries(pinball_fuzz PRIVATE pinball_fuzz_core)
  target_link_options(pinball_fuzz PRIVATE -fsanitize=fuzzer)
  return()
endif()

if(PINBALL_FETCH_DEPS)
  include(cmake/PinnedDeps.cmake)
else()
  # 3.4 is the first release with the built-in PNG loader used for assets.
  find_package(SDL3 3.4 REQUIRED CONFIG)
  find_package(box2d 3.2 REQUIRED CONFIG)
endif()

# Platform-independent game logic. No SDL dependency so it is unit-testable headless.
add_library(pinball_core STATIC
  src/core/Args.cpp
  src/core/AssetManifest.cpp
  src/core/AssetName.cpp
  src/core/Format.cpp
  src/core/Game.cpp
  src/core/HighScores.cpp
  src/core/Missions.cpp
  src/core/Physics.cpp
  src/core/PngHeader.cpp
  src/core/Rules.cpp
  src/core/Sha256.cpp
  src/core/Music.cpp
  src/core/Sound.cpp
  src/core/TableLayout.cpp
)
target_include_directories(pinball_core PUBLIC src ${PINBALL_VERSION_DIR})
target_link_libraries(pinball_core PUBLIC box2d::box2d)
# PUBLIC so the same warnings, hardening and sanitizer flags reach every consumer.
pinball_apply_hardening(pinball_core PUBLIC)

# SDL-facing code shared by the game and the snapshot tool.
add_library(pinball_platform STATIC
  src/platform/About.cpp
  src/platform/App.cpp
  src/platform/Assets.cpp
  src/platform/AudioOutput.cpp
  src/platform/EmbeddedManifest.cpp
  src/platform/Hud.cpp
  src/platform/InitialsScreen.cpp
  src/platform/SafeFile.cpp
  src/platform/ScoreStore.cpp
  src/platform/TableRenderer.cpp
  src/platform/Text.cpp
)
target_link_libraries(pinball_platform PUBLIC pinball_core SDL3::SDL3)
if(APPLE AND NOT PINBALL_IOS)
  # The menu bar's About item is routed to the game's own About screen.
  target_sources(pinball_platform PRIVATE src/platform/MacMenu.mm)
  set_source_files_properties(src/platform/MacMenu.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(pinball_platform PRIVATE "-framework Cocoa")
endif()

if(PINBALL_IOS)
  # SPIKE: an iOS application bundle. Flat layout: the executable, Info.plist
  # and the assets directory sit directly in Pinball.app. Not signed here
  # (the simulator needs no signature; a device build is only compiled).
  # The assets directory goes in as an Xcode folder reference, copied by the
  # app target's own "Copy Bundle Resources" phase (on iOS "Resources" is the
  # bundle root). The pinball_assets custom target cannot be used with the
  # Xcode generator for iOS: $<TARGET_BUNDLE_DIR> expands to
  # Release${EFFECTIVE_PLATFORM_NAME}/Pinball.app and a VERBATIM custom
  # command escapes the "$", so the copy lands in a directory literally
  # named "Release${EFFECTIVE_PLATFORM_NAME}".
  add_executable(pinball MACOSX_BUNDLE src/main.cpp ${CMAKE_SOURCE_DIR}/assets)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/assets PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(pinball PROPERTIES
    OUTPUT_NAME "${PINBALL_IOS_NAME}"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info-iOS.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "${PINBALL_IOS_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${PINBALL_BUNDLE_ID}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${PINBALL_BUNDLE_ID}
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
elseif(APPLE)
  # On macOS the game is an application bundle, "Space Cadet Pinball.app",
  # with the executable inside under the same name: the Dock, the menu bar
  # and Activity Monitor all take their label from one or the other. The
  # CMake target is still called pinball.
  set(PINBALL_ICON ${CMAKE_SOURCE_DIR}/packaging/macos/AppIcon.icns)
  add_executable(pinball MACOSX_BUNDLE src/main.cpp ${PINBALL_ICON})
  set_source_files_properties(${PINBALL_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # LSMinimumSystemVersion: the deployment target, or with none set the
  # macOS this is built on, which is what the compiler then targets.
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(PINBALL_MIN_MACOS ${CMAKE_OSX_DEPLOYMENT_TARGET})
  else()
    execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE PINBALL_MIN_MACOS
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PINBALL_MIN_MACOS MATCHES "^[0-9]+\\.[0-9]+")
      set(PINBALL_MIN_MACOS "11.0")
    endif()
  endif()
  set_target_properties(pinball PROPERTIES
    OUTPUT_NAME "Space Cadet Pinball"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Space Cadet Pinball"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${PINBALL_BUNDLE_ID}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE AppIcon
    MACOSX_BUNDLE_COPYRIGHT "MIT licence. An independent fan project.")
else()
  add_executable(pinball src/main.cpp)
endif()
target_link_libraries(pinball PRIVATE pinball_platform)
# A Release game carries no debug map, so it names no directory of the
# machine that built it.
if(APPLE)
  target_link_options(pinball PRIVATE $<$<CONFIG:Release>:-Wl,-S>)
elseif(NOT MSVC)
  # And no symbol names either.
  target_link_options(pinball PRIVATE $<$<CONFIG:Release>:-Wl,-s>)
endif()

# Where the things that sit beside the game go: the development tools, the
# tests' scratch directories and the frames they render. Beside the bundle
# on macOS, never inside it, because anything added to a signed bundle
# breaks its signature. Elsewhere it is the directory of the executable, as
# before.
if(PINBALL_IOS)
  # SPIKE: SDL_GetBasePath() is the bundle directory itself on iOS.
  set(PINBALL_BESIDE_DIR $<TARGET_BUNDLE_DIR:pinball>/..)
  set(PINBALL_GAME_ASSETS_DIR $<TARGET_BUNDLE_DIR:pinball>/assets)
elseif(APPLE)
  set(PINBALL_BESIDE_DIR $<TARGET_BUNDLE_DIR:pinball>/..)
  set(PINBALL_GAME_ASSETS_DIR $<TARGET_BUNDLE_CONTENT_DIR:pinball>/Resources/assets)
else()
  set(PINBALL_BESIDE_DIR $<TARGET_FILE_DIR:pinball>)
  set(PINBALL_GAME_ASSETS_DIR $<TARGET_FILE_DIR:pinball>/assets)
endif()

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

# The game loads assets from an "assets" directory under SDL's base path:
# next to the executable, or Contents/Resources inside the macOS bundle.
# Copied by a target that runs on every build (not only on relink), so an
# edited asset is never stale. Nothing is ever deleted: a removed asset leaves
# an unused file behind in the build tree, which is harmless (on macOS the
# manifest check never opens it, but a signed bundle lists it until the next
# clean build).
if(NOT PINBALL_IOS)
add_custom_target(pinball_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/assets ${PINBALL_GAME_ASSETS_DIR}
  COMMENT "Copying assets"
  VERBATIM)
# The generator expressions add no dependency, so this is not circular. It
# makes building just the pinball target bring its assets along.
add_dependencies(pinball pinball_assets)
endif()

# macOS hardened runtime: the system then ignores DYLD_INSERT_LIBRARIES and
# refuses to load any library that is not Apple's. That needs the game to be
# self-contained, so it is tied to the statically linked pinned dependencies.
# Applied to Release builds. By default the signature is ad hoc: it turns the
# protections on but does not identify a publisher. PINBALL_CODESIGN_IDENTITY
# names a certificate to sign with instead (a Developer ID Application one
# for a build that is to be notarised: tools/notarize.sh). No entitlements
# are used: the game needs no network, no JIT and no exemption.
#
# The whole bundle is signed, which seals the Info.plist, the icon and the
# assets with the executable. Signing therefore has to come last, and it is
# its own target that runs on every build, after the executable is linked and
# the assets are copied: a POST_BUILD step would only run on a relink and
# leave the seal stale after an asset changed. Anything that alters the
# bundle afterwards invalidates the signature until the next build.
if(APPLE AND NOT PINBALL_IOS)
  set(PINBALL_CODESIGN_IDENTITY "" CACHE STRING
    "Code signing identity for the macOS bundle (empty: ad hoc), e.g. Developer ID Application: NAME (TEAMID)")
  set(pinball_sign OFF)
  if(PINBALL_FETCH_DEPS AND NOT PINBALL_SANITIZE)
    option(PINBALL_HARDENED_RUNTIME "Sign the game with the hardened runtime" ON)
    set(pinball_sign ${PINBALL_HARDENED_RUNTIME})
  endif()
  if(PINBALL_CODESIGN_IDENTITY AND NOT pinball_sign)
    message(FATAL_ERROR "PINBALL_CODESIGN_IDENTITY signs with the hardened runtime, which needs "
                        "-DPINBALL_FETCH_DEPS=ON, no sanitizers and PINBALL_HARDENED_RUNTIME left ON")
  endif()
  if(pinball_sign)
    if(PINBALL_CODESIGN_IDENTITY)
      # A secure timestamp, which notarisation requires (needs the network).
      set(pinball_sign_args --sign ${PINBALL_CODESIGN_IDENTITY} --options runtime --timestamp)
    else()
      set(pinball_sign_args --sign - --options runtime)
    endif()
    # Release only: a debugger cannot attach to a hardened process, so the
    # configurations meant for debugging are left alone.
    add_custom_target(pinball_sign ALL
      COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Release>,env,true>
              codesign --force ${pinball_sign_args} $<TARGET_BUNDLE_DIR:pinball>
      COMMENT "Signing the bundle with the hardened runtime (Release only)"
      VERBATIM)
    add_dependencies(pinball_sign pinball)
  endif()
endif()

# Development tool that renders a frame offscreen to a PNG. Not part of the
# game; built with the tests.
option(PINBALL_BUILD_TOOLS "Build development tools" ON)
if(PINBALL_BUILD_TOOLS)
  add_executable(pinball_snapshot tools/snapshot/main.cpp)
  target_link_libraries(pinball_snapshot PRIVATE pinball_platform)
  # It sits beside the game and reads an assets directory next to itself,
  # through the same verifying loader. Off macOS that is the game's own
  # directory. On macOS the tools stay out of the bundle and get a copy of
  # their own, kept up to date the same way.
  set_target_properties(pinball_snapshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PINBALL_BESIDE_DIR})
  if(APPLE)
    add_custom_target(pinball_tool_assets
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              ${CMAKE_SOURCE_DIR}/assets ${PINBALL_BESIDE_DIR}/assets
      COMMENT "Copying assets for the tools"
      VERBATIM)
    add_dependencies(pinball_snapshot pinball_tool_assets)
  else()
    add_dependencies(pinball_snapshot pinball_assets)
  endif()

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
    tests/HighScoreTest.cpp
    tests/HoleTest.cpp
    tests/MissionTest.cpp
    tests/NudgeTest.cpp
    tests/PhysicsTest.cpp
    tests/PlungerTest.cpp
    tests/PngHeaderTest.cpp
    tests/RulesTest.cpp
    tests/Sha256Test.cpp
    tests/MusicTest.cpp
    tests/SoundTest.cpp
    tests/TargetTest.cpp
  )
  target_link_libraries(core_tests PRIVATE pinball_core)
  add_test(NAME core_tests COMMAND core_tests)

  # Scratch and output files go beside the binaries under test (beside the
  # bundle on macOS, not in it), so with a multi-config generator each
  # configuration has its own.
  set(PINBALL_TEST_DIR ${PINBALL_BESIDE_DIR})
  # Whatever a test starts gets a per-user preferences directory inside the
  # build tree, so a test run cannot touch the player's real high score file.
  # XDG_DATA_HOME is how SDL is told on Linux; macOS has no such switch.
  set(PINBALL_HEADLESS_ENV
    "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test_prefs")

  # File handling: bounded reads, refusal of links and pipes, atomic saves.
  add_executable(safefile_tests tests/SafeFileTest.cpp)
  target_link_libraries(safefile_tests PRIVATE pinball_platform)
  add_test(NAME safe_files
    COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:safefile_tests>
            -DSCRATCH=$<TARGET_FILE_DIR:safefile_tests>/safefile_scratch
            -P ${CMAKE_SOURCE_DIR}/tests/RunInScratch.cmake)

  # The same tests with SafeFile.cpp built without the single-call
  # move-without-replace (renameat2, renamex_np), so that the link() and
  # unlink() path other systems and file systems fall back to is compiled
  # and run on every system. SafeFile.cpp is compiled here a second time:
  # the copy in pinball_platform is the normal one. On a system that is not
  # POSIX the macro changes nothing and this repeats safe_files.
  add_executable(safefile_fallback_tests tests/SafeFileTest.cpp src/platform/SafeFile.cpp)
  target_compile_definitions(safefile_fallback_tests PRIVATE PINBALL_NO_ATOMIC_NOREPLACE)
  # The include path, warnings and hardening come with pinball_core; SDL is
  # for SafeFile's portable half. Not pinball_platform, which has the normal
  # SafeFile in it.
  target_link_libraries(safefile_fallback_tests PRIVATE pinball_core SDL3::SDL3)
  add_test(NAME safe_files_fallback
    COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:safefile_fallback_tests>
            -DSCRATCH=$<TARGET_FILE_DIR:safefile_fallback_tests>/safefile_fallback_scratch
            -P ${CMAKE_SOURCE_DIR}/tests/RunInScratch.cmake)

  # Python runs isolated (-I): no user site directory, no PYTHON* variables
  # and no import from the script's or the current directory.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # Assets must be well-formed PNGs with no metadata.
    add_test(NAME assets_clean
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_assets.py
              ${CMAKE_SOURCE_DIR}/assets)

    # The built game must refuse to start if any asset is altered.
    add_test(NAME asset_integrity
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/test_asset_integrity.py
              $<TARGET_FILE:pinball> ${PINBALL_TEST_DIR}/asset_integrity_scratch)
    set_tests_properties(asset_integrity PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 120)
  elseif(PINBALL_REQUIRE_ALL_TESTS)
    message(FATAL_ERROR "Python 3 not found, and PINBALL_REQUIRE_ALL_TESTS is ON: the "
                        "assets_clean, asset_integrity and layout_json tests need it")
  else()
    # Say so: a run without these would otherwise look complete.
    message(WARNING "Python 3 not found: the assets_clean, asset_integrity and "
                    "layout_json tests are not registered")
  endif()
  if(PINBALL_REQUIRE_ALL_TESTS AND NOT PINBALL_BUILD_TOOLS)
    message(FATAL_ERROR "PINBALL_BUILD_TOOLS is OFF, and PINBALL_REQUIRE_ALL_TESTS is ON: the "
                        "render and layout tests need the tools")
  endif()

  # Rendering smoke test: play ten seconds headless and draw a frame. The
  # tool fails if the frame is blank.
  if(PINBALL_BUILD_TOOLS)
    add_test(NAME render_snapshot
      COMMAND pinball_snapshot 1200 render_snapshot.png
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    add_test(NAME render_about
      COMMAND pinball_snapshot 60 render_about.png about
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    set_tests_properties(render_about PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    add_test(NAME render_initials
      COMMAND pinball_snapshot 60 render_initials.png initials
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    set_tests_properties(render_initials PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    set_tests_properties(render_snapshot PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    # The tool must replace a link at its output name, never write through
    # it, even one that points nowhere. (Not on Windows, where making a link
    # needs a privilege.)
    if(UNIX)
      add_test(NAME snapshot_replaces_link
        COMMAND ${CMAKE_COMMAND} -DSNAPSHOT=$<TARGET_FILE:pinball_snapshot>
                -DSCRATCH=${PINBALL_TEST_DIR}/snapshot_link_scratch
                -P ${CMAKE_SOURCE_DIR}/tests/SnapshotReplacesLink.cmake)
      set_tests_properties(snapshot_replaces_link PROPERTIES
        ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    endif()
  endif()

  # The geometry dump must be valid JSON.
  if(PINBALL_BUILD_TOOLS AND Python3_Interpreter_FOUND)
    add_test(NAME layout_json
      COMMAND Python3::Interpreter -I -c
              "import json,subprocess,sys; d=json.loads(subprocess.check_output([sys.argv[1]])); assert len(d['shell'])>10 and d['bumpers'] and d['holes']"
              $<TARGET_FILE:pinball_layout>)
  endif()

  # Headless smoke test: run the real binary with no display. The binary itself
  # fails if no simulation step ran.
  add_test(NAME smoke_headless COMMAND pinball --frames 120)
  set_tests_properties(smoke_headless PROPERTIES
    ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 30)

  # The macOS bundle: a valid Info.plist that carries the identifier and
  # version the game itself reports, the icon, the assets, and no stray
  # executable.
  if(APPLE)
    add_test(NAME bundle_layout
      COMMAND ${CMAKE_COMMAND} -DBUNDLE=$<TARGET_BUNDLE_DIR:pinball>
              -DEXECUTABLE=$<TARGET_FILE:pinball>
              -DIDENTIFIER=${PINBALL_BUNDLE_ID} -DVERSION=${PROJECT_VERSION}
              -P ${CMAKE_SOURCE_DIR}/tests/CheckBundle.cmake)
    set_tests_properties(bundle_layout PROPERTIES TIMEOUT 30)
  endif()

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
