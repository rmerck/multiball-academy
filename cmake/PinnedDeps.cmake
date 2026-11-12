# Builds SDL3 and Box2D from source at fixed, verified revisions and links
# them statically. Used by CI and for any build that is handed to someone
# else: the result does not depend on what a package manager serves that
# day. On macOS the game then loads only system libraries. (On Linux SDL
# still opens the system's display and audio libraries at run time.)
#
# When updating: tools/check_pins.sh reports newer upstream releases.
include(FetchContent)

# FetchContent has switches that swap in other sources without checking
# them: a source directory named on the command line is used as it is, and a
# disconnected build uses whatever is already in the build tree. Either
# would quietly undo the pinning, so both stop the configuration unless
# asked for by name.
option(PINBALL_ALLOW_UNVERIFIED_DEPS
  "Allow FetchContent overrides that replace the pinned, verified SDL3 and Box2D sources" OFF)
set(pinball_unverified "")
foreach(name IN ITEMS SDL3 BOX2D)
  if(DEFINED FETCHCONTENT_SOURCE_DIR_${name}
     AND NOT "${FETCHCONTENT_SOURCE_DIR_${name}}" STREQUAL "")
    list(APPEND pinball_unverified
      "FETCHCONTENT_SOURCE_DIR_${name}=${FETCHCONTENT_SOURCE_DIR_${name}}")
  endif()
endforeach()
if(FETCHCONTENT_FULLY_DISCONNECTED)
  list(APPEND pinball_unverified
    "FETCHCONTENT_FULLY_DISCONNECTED=${FETCHCONTENT_FULLY_DISCONNECTED}")
endif()
# The same by another road: an installed package in place of the sources.
if("${FETCHCONTENT_TRY_FIND_PACKAGE_MODE}" STREQUAL "ALWAYS")
  list(APPEND pinball_unverified "FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS")
endif()
if(pinball_unverified)
  list(JOIN pinball_unverified ", " pinball_unverified_text)
  if(PINBALL_ALLOW_UNVERIFIED_DEPS)
    message(WARNING "PINBALL_ALLOW_UNVERIFIED_DEPS is ON: SDL3 or Box2D may be built from sources "
                    "that were not checked against the pinned hash or commit "
                    "(${pinball_unverified_text}).")
  else()
    message(FATAL_ERROR "PINBALL_FETCH_DEPS builds SDL3 and Box2D from pinned, verified sources, "
                        "but this setting replaces them without any check: "
                        "${pinball_unverified_text}. Remove it, or pass "
                        "-DPINBALL_ALLOW_UNVERIFIED_DEPS=ON to accept unverified sources.")
  endif()
endif()

# The dependencies are most of the program, so they get the same protective
# flags as the game's own code (not its warnings). The flags are set on the
# top directory, because that is where this file is included: they reach the
# dependencies added below and, a second time and harmlessly, the game's own
# targets.
pinball_dependency_hardening()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# The release archive, checked against its SHA-256. Files keep the dates
# they have in the archive, not the time of unpacking: the sources then look
# the same age on every machine, and objects compiled from them earlier (CI
# keeps SDL's between runs) are still seen as up to date.
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
  URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)

set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
# Tag v3.2.0, named by its commit so the tag cannot be moved under us.
FetchContent_Declare(box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG 178495ba35695aefe9c321e569d2277f06db3b90
  SYSTEM)

FetchContent_MakeAvailable(SDL3 box2d)
