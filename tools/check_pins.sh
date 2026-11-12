#!/bin/sh
# Reports whether the dependency versions pinned in cmake/PinnedDeps.cmake
# are still the newest upstream releases. Exits 1 if any is behind.
# Reads tag names only; downloads and runs nothing.
set -eu

pins="$(dirname "$0")/../cmake/PinnedDeps.cmake"
status=0

newest() { # <repository url> <tag pattern>
  git ls-remote --tags --refs --sort=-v:refname "$1" "$2" | head -n 1 | sed 's|.*refs/tags/||'
}

sdl_pinned=$(sed -n 's|.*/download/\(release-[0-9.]*\)/.*|\1|p' "$pins" | head -n 1)
# Same minor series only: a new series may change behaviour and needs a look.
sdl_series=$(printf '%s' "$sdl_pinned" | sed 's|\(release-[0-9]*\.[0-9]*\)\..*|\1|')
sdl_newest=$(newest https://github.com/libsdl-org/SDL "${sdl_series}.*")
if [ "$sdl_pinned" != "$sdl_newest" ]; then
  echo "SDL: pinned $sdl_pinned, newest $sdl_newest"
  status=1
fi

box2d_pinned=$(sed -n 's|.*GIT_TAG \([0-9a-f]\{40\}\).*|\1|p' "$pins" | head -n 1)
box2d_tag=$(newest https://github.com/erincatto/box2d 'v*')
box2d_newest=$(git ls-remote https://github.com/erincatto/box2d "refs/tags/$box2d_tag^{}" "refs/tags/$box2d_tag" | tail -n 1 | cut -f1)
if [ "$box2d_pinned" != "$box2d_newest" ]; then
  echo "Box2D: pinned $box2d_pinned, newest $box2d_tag is $box2d_newest"
  status=1
fi

[ "$status" -eq 0 ] && echo "Pinned dependencies are current: $sdl_pinned, Box2D $box2d_tag"
exit "$status"
