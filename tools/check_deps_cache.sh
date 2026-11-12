#!/bin/sh
# Run by CI between restoring its cache and configuring.
# Usage: check_deps_cache.sh <build directory>
#
# The cache is meant to hold SDL's build output, <build>/_deps/sdl3-build
# (its objects and the files its build generates), and nothing else: the
# sources of SDL and Box2D are always fetched and verified afresh
# (cmake/PinnedDeps.cmake). If a restore has put anything else in the build
# directory (a source tree, the stamps that tell FetchContent a download is
# already done, a CMake cache), the build would use it unchecked, so this
# fails instead. The three directories must be real ones, not links. Exits 0
# when the build directory is absent (nothing was restored) or holds only
# what it should, 1 otherwise. It cannot tell whether the objects themselves
# are the ones CI compiled; see the note in .github/workflows/ci.yml.
set -eu

[ $# -eq 1 ] || { echo "usage: check_deps_cache.sh <build directory>" >&2; exit 1; }
build=$1
objects="$build/_deps/sdl3-build"

if [ ! -e "$build" ] && [ ! -L "$build" ]; then
  echo "no restored cache"
  exit 0
fi

# Each of the three levels is a real directory holding just the next one.
for path in "$build" "$build/_deps" "$objects"; do
  if [ -L "$path" ] || [ ! -d "$path" ]; then
    echo "restored cache: $path is not a directory" >&2
    exit 1
  fi
done
extra=$(find "$build" -mindepth 1 -maxdepth 2 \
  ! -path "$build/_deps" ! -path "$objects" | head -n 5)
if [ -n "$extra" ]; then
  echo "restored cache holds more than SDL's objects:" >&2
  echo "$extra" >&2
  exit 1
fi
echo "restored cache holds only $objects"
