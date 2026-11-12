#!/bin/sh
# Reports whether the dependency versions pinned in cmake/PinnedDeps.cmake
# are still the newest upstream releases. Exits 1 if any is behind, and 2 if
# it could not tell (a pin it cannot read, or no answer from upstream).
# Reads tag names only; downloads and runs nothing.
set -eu

pins="$(dirname "$0")/../cmake/PinnedDeps.cmake"
status=0

# Stops with "could not check" unless the value is there.
need() { # <value> <what it is>
  [ -n "$1" ] || { echo "cannot determine $2" >&2; exit 2; }
}

newest() { # <repository url> <tag pattern>
  # A suffix such as -rc1 sorts before the release it leads up to.
  git -c versionsort.suffix=- ls-remote --tags --refs --sort=-v:refname "$1" "$2" |
    head -n 1 | sed 's|.*refs/tags/||'
}

sdl_pinned=$(sed -n 's|.*/download/\(release-[0-9.]*\)/.*|\1|p' "$pins" | head -n 1)
# Same minor series only: a new series may change behaviour and needs a look.
need "$sdl_pinned" "the pinned SDL release"
sdl_series=$(printf '%s' "$sdl_pinned" | sed 's|\(release-[0-9]*\.[0-9]*\)\..*|\1|')
sdl_newest=$(newest https://github.com/libsdl-org/SDL "${sdl_series}.*")
need "$sdl_newest" "the newest SDL release"
if [ "$sdl_pinned" != "$sdl_newest" ]; then
  echo "SDL: pinned $sdl_pinned, newest $sdl_newest"
  status=1
fi

box2d_pinned=$(sed -n 's|.*GIT_TAG \([0-9a-f]\{40\}\).*|\1|p' "$pins" | head -n 1)
need "$box2d_pinned" "the pinned Box2D commit"
box2d_tag=$(newest https://github.com/erincatto/box2d 'v*')
need "$box2d_tag" "the newest Box2D release"
box2d_newest=$(git ls-remote https://github.com/erincatto/box2d "refs/tags/$box2d_tag^{}" "refs/tags/$box2d_tag" | tail -n 1 | cut -f1)
need "$box2d_newest" "the newest Box2D commit"
if [ "$box2d_pinned" != "$box2d_newest" ]; then
  echo "Box2D: pinned $box2d_pinned, newest $box2d_tag is $box2d_newest"
  status=1
fi

[ "$status" -eq 0 ] && echo "Pinned dependencies are current: $sdl_pinned, Box2D $box2d_tag"
exit "$status"
