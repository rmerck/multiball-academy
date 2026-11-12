# Builds SDL3 and Box2D from source at fixed, verified revisions and links
# them statically. Used by CI and for any build that is handed to someone
# else: the result does not depend on what a package manager serves that
# day. On macOS the game then loads only system libraries. (On Linux SDL
# still opens the system's display and audio libraries at run time.)
#
# When updating: tools/check_pins.sh reports newer upstream releases.
include(FetchContent)

# The dependencies are most of the program, so they get the same protective
# flags as the game's own code (not its warnings). Set for this directory,
# which covers everything added below.
pinball_dependency_hardening()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# The release archive, checked against its SHA-256.
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
  URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
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
