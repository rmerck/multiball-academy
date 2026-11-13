# Optional, for an iOS configuration: `cmake ... -C cmake/IosPreseed.cmake`,
# after -DCMAKE_SYSTEM_NAME and -DCMAKE_OSX_SYSROOT on the command line.
#
# SDL asks the compiler some 350 small questions when it is configured ("is
# there a strlcpy?"), and with the Xcode generator each one is a run of
# xcodebuild: about ten minutes on a CI runner. The answers depend only on
# the Xcode in use, the SDK, the deployment target and the SDL being built.
# cmake/preseed holds the answers recorded from a full configuration, one
# file per Xcode build and SDK; this script puts them in the cache so the
# questions are not asked again.
#
# A file is used only if all four keys in its header match this
# configuration: Xcode's build version, the SDK, the deployment target, and
# the SHA-256 of cmake/PinnedDeps.cmake (the SDL pin and its options). With
# no such file every question is asked as usual, and a notice says so. To
# record a new file after an Xcode or SDL update, see docs/BUILDING.md.
#
# A preseed file is never run as CMake code. It is read line by line here,
# and only entries of the form NAME=1 or NAME= whose name is one of SDL's
# probe results (cmake/IosPreseedKeys.cmake) are accepted; any other line
# rejects the whole file. So no file in cmake/preseed can change which
# sources are fetched, the pinned hash, or any option.
include("${CMAKE_CURRENT_LIST_DIR}/IosPreseedKeys.cmake")

set(PINBALL_IOS_PRESEED "" CACHE INTERNAL "The preseed file applied to this build, if any" FORCE)

if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
  message(NOTICE "IosPreseed: not an iOS configuration (give -DCMAKE_SYSTEM_NAME=iOS before -C): nothing applied")
  return()
endif()
if("${CMAKE_OSX_SYSROOT}" MATCHES "^(iphoneos|iphonesimulator)$")
  set(pinball_preseed_sdk "${CMAKE_OSX_SYSROOT}")
elseif("${CMAKE_OSX_SYSROOT}" STREQUAL "")
  set(pinball_preseed_sdk "iphoneos")
else()
  message(NOTICE "IosPreseed: unrecognised CMAKE_OSX_SYSROOT: nothing applied")
  return()
endif()
# The deployment target: the command line's, or the default CMakeLists.txt
# is about to set.
set(pinball_preseed_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(pinball_preseed_target STREQUAL "")
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" pinball_preseed_line
       REGEX "CMAKE_OSX_DEPLOYMENT_TARGET \"[0-9.]+\" CACHE STRING \"Minimum iOS version\"")
  if(pinball_preseed_line MATCHES "CMAKE_OSX_DEPLOYMENT_TARGET \"([0-9.]+)\"")
    set(pinball_preseed_target "${CMAKE_MATCH_1}")
  endif()
endif()
pinball_preseed_keys()

set(pinball_preseed_file
  "${CMAKE_CURRENT_LIST_DIR}/preseed/sdl-ios-${pinball_preseed_xcode}-${pinball_preseed_sdk}.txt")
if(pinball_preseed_xcode STREQUAL "" OR NOT EXISTS "${pinball_preseed_file}")
  message(NOTICE "IosPreseed: no recorded results for Xcode build '${pinball_preseed_xcode}' and "
                 "${pinball_preseed_sdk}: SDL's checks will all run")
  return()
endif()

file(STRINGS "${pinball_preseed_file}" pinball_preseed_lines)
set(pinball_preseed_expected
  "# xcode: ${pinball_preseed_xcode}"
  "# sdk: ${pinball_preseed_sdk}"
  "# deployment-target: ${pinball_preseed_target}"
  "# pinned-deps-sha256: ${pinball_preseed_deps}")
set(pinball_preseed_names "")
set(pinball_preseed_values "")
set(pinball_preseed_reason "")
foreach(line IN LISTS pinball_preseed_lines)
  if(line MATCHES "^# (xcode|sdk|deployment-target|pinned-deps-sha256): ")
    list(FIND pinball_preseed_expected "${line}" pinball_preseed_found)
    if(pinball_preseed_found EQUAL -1)
      set(pinball_preseed_reason "it was recorded for another configuration (${line})")
      break()
    endif()
    list(REMOVE_AT pinball_preseed_expected ${pinball_preseed_found})
  elseif(line MATCHES "^#" OR line STREQUAL "")
    # A comment.
  elseif(line MATCHES "^([A-Za-z0-9_]+)=(1?)$" AND CMAKE_MATCH_1 MATCHES "${PINBALL_PRESEED_NAME}")
    list(APPEND pinball_preseed_names "${CMAKE_MATCH_1}")
    list(APPEND pinball_preseed_values "x${CMAKE_MATCH_2}")
  else()
    set(pinball_preseed_reason "it holds a line that is not a probe result")
    break()
  endif()
endforeach()
if(pinball_preseed_reason STREQUAL "" AND pinball_preseed_expected)
  set(pinball_preseed_reason "its header lacks a key")
endif()
if(NOT pinball_preseed_reason STREQUAL "")
  message(NOTICE "IosPreseed: ${pinball_preseed_file} is not used: ${pinball_preseed_reason}. "
                 "SDL's checks will all run")
  return()
endif()

list(LENGTH pinball_preseed_names pinball_preseed_count)
if(pinball_preseed_count GREATER 0)
  math(EXPR pinball_preseed_last "${pinball_preseed_count} - 1")
  foreach(index RANGE ${pinball_preseed_last})
    list(GET pinball_preseed_names ${index} pinball_preseed_name)
    list(GET pinball_preseed_values ${index} pinball_preseed_value)
    string(SUBSTRING "${pinball_preseed_value}" 1 -1 pinball_preseed_value)
    set(${pinball_preseed_name} "${pinball_preseed_value}" CACHE INTERNAL "Recorded result (cmake/preseed)")
  endforeach()
endif()
get_filename_component(pinball_preseed_shown "${pinball_preseed_file}" NAME)
set(PINBALL_IOS_PRESEED "${pinball_preseed_shown}" CACHE INTERNAL
    "The preseed file applied to this build, if any" FORCE)
message(NOTICE "IosPreseed: ${pinball_preseed_count} recorded results applied from ${pinball_preseed_shown}")
