# The guard that keeps a placeholder iOS icon out of a shippable build
# (cmake/IosIconCheck.cmake): the committed icon passes it, and a marker
# file, a known placeholder's hash, or a missing icon each trip it.
# Usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH=<dir> -P CheckIosIconGuard.cmake
cmake_minimum_required(VERSION 3.26)
include(${SOURCE_DIR}/cmake/IosIconCheck.cmake)
set(icon ${SOURCE_DIR}/packaging/ios/Assets.xcassets/AppIcon.appiconset/icon_1024.png)
set(marker ${SOURCE_DIR}/packaging/ios/ICON_IS_A_PLACEHOLDER)

# The placeholder that was once committed stays refused: its hash may not
# fall off the list.
list(FIND PINBALL_IOS_PLACEHOLDER_ICON_HASHES
  62cae5f0b7bbcca630794c23a725009451cac0e2c6bee2b507655b3ff99709bb found)
if(found EQUAL -1)
  message(FATAL_ERROR "the historical placeholder icon's hash is no longer refused")
endif()

# The real icon, as committed: no marker, not a placeholder's hash.
pinball_ios_icon_is_placeholder(placeholder ICON ${icon} MARKER ${marker})
if(placeholder)
  message(FATAL_ERROR "the committed iOS icon is taken for a placeholder")
endif()

file(REMOVE_RECURSE ${SCRATCH})
file(MAKE_DIRECTORY ${SCRATCH})
# A marker beside a real icon.
file(WRITE ${SCRATCH}/ICON_IS_A_PLACEHOLDER "marker\n")
pinball_ios_icon_is_placeholder(placeholder ICON ${icon} MARKER ${SCRATCH}/ICON_IS_A_PLACEHOLDER)
if(NOT placeholder)
  message(FATAL_ERROR "a marker file did not trip the guard")
endif()
# A placeholder known by its hash, with no marker.
file(WRITE ${SCRATCH}/placeholder.png "a placeholder icon\n")
file(SHA256 ${SCRATCH}/placeholder.png hash)
pinball_ios_icon_is_placeholder(placeholder ICON ${SCRATCH}/placeholder.png
  MARKER ${SCRATCH}/no-marker HASHES ${PINBALL_IOS_PLACEHOLDER_ICON_HASHES} ${hash})
if(NOT placeholder)
  message(FATAL_ERROR "a known placeholder hash did not trip the guard")
endif()
# The same file is fine once its hash is not on the list.
pinball_ios_icon_is_placeholder(placeholder ICON ${SCRATCH}/placeholder.png
  MARKER ${SCRATCH}/no-marker)
if(placeholder)
  message(FATAL_ERROR "an unknown icon was taken for a placeholder")
endif()
# No icon at all.
pinball_ios_icon_is_placeholder(placeholder ICON ${SCRATCH}/missing.png
  MARKER ${SCRATCH}/no-marker)
if(NOT placeholder)
  message(FATAL_ERROR "a missing icon did not trip the guard")
endif()
file(REMOVE_RECURSE ${SCRATCH})
