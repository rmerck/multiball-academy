# Checks that pinball_snapshot replaces a symbolic link found at its output
# name and never writes through it: neither a link to an existing file nor
# one that points nowhere.
# Usage: cmake -DSNAPSHOT=<exe> -DSCRATCH=<dir> -P SnapshotReplacesLink.cmake
if(NOT DEFINED SNAPSHOT OR NOT DEFINED SCRATCH)
  message(FATAL_ERROR "SNAPSHOT and SCRATCH are required")
endif()
# Only ever remove a directory an earlier run of this test left behind.
if(NOT SCRATCH MATCHES "/snapshot_link_scratch$")
  message(FATAL_ERROR "refusing to use ${SCRATCH}")
endif()
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# A link to nothing, and a link to a file that must stay as it is. The link
# texts are relative, so they resolve inside the scratch directory.
file(CREATE_LINK absent.png "${SCRATCH}/dangling.png" SYMBOLIC)
file(WRITE "${SCRATCH}/victim.png" "untouched")
file(CREATE_LINK victim.png "${SCRATCH}/linked.png" SYMBOLIC)
if(NOT IS_SYMLINK "${SCRATCH}/dangling.png" OR NOT IS_SYMLINK "${SCRATCH}/linked.png")
  message(FATAL_ERROR "could not plant the links")
endif()

foreach(name IN ITEMS dangling linked)
  execute_process(COMMAND "${SNAPSHOT}" 60 ${name}.png
    WORKING_DIRECTORY "${SCRATCH}" RESULT_VARIABLE code TIMEOUT 50)
  if(NOT code STREQUAL "0")
    message(FATAL_ERROR "pinball_snapshot failed for ${name}.png: ${code}")
  endif()
  if(IS_SYMLINK "${SCRATCH}/${name}.png")
    message(FATAL_ERROR "${name}.png is still a link: it was written through")
  endif()
  # What was written is a PNG.
  file(READ "${SCRATCH}/${name}.png" signature LIMIT 8 HEX)
  if(NOT signature STREQUAL "89504e470d0a1a0a")
    message(FATAL_ERROR "${name}.png is not a PNG")
  endif()
endforeach()

if(EXISTS "${SCRATCH}/absent.png")
  message(FATAL_ERROR "the dangling link's target was created")
endif()
file(READ "${SCRATCH}/victim.png" victim)
if(NOT victim STREQUAL "untouched")
  message(FATAL_ERROR "the link's target was overwritten")
endif()
file(GLOB entries RELATIVE "${SCRATCH}" "${SCRATCH}/*")
list(SORT entries)
if(NOT entries STREQUAL "dangling.png;linked.png;victim.png")
  message(FATAL_ERROR "unexpected files left behind: ${entries}")
endif()
file(REMOVE_RECURSE "${SCRATCH}")
