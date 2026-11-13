# Records SDL's configure results from a finished iOS configuration into
# cmake/preseed, for cmake/IosPreseed.cmake to apply next time.
# Usage: cmake -DBUILD=<iOS build directory> [-DOUTPUT_DIR=<directory>]
#              -P cmake/MakeIosPreseed.cmake
# The build directory must have been configured in full, without a preseed
# file: results are recorded only from checks that were really run. Run it
# with the same Xcode selected (DEVELOPER_DIR) as the configuration.
cmake_minimum_required(VERSION 3.26)
if(NOT DEFINED BUILD)
  message(FATAL_ERROR "BUILD is required: the iOS build directory")
endif()
if(NOT DEFINED OUTPUT_DIR)
  set(OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/preseed")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/IosPreseedKeys.cmake")

set(cache "${BUILD}/CMakeCache.txt")
if(NOT EXISTS "${cache}")
  message(FATAL_ERROR "no CMakeCache.txt in ${BUILD}")
endif()
file(STRINGS "${cache}" entries REGEX "^[A-Za-z0-9_]+:[A-Z]+=")

# One cache entry's value, or "" if absent.
function(cache_value name out)
  set(value "")
  foreach(entry IN LISTS entries)
    if(entry MATCHES "^${name}:[A-Z]+=(.*)$")
      set(value "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

cache_value(CMAKE_SYSTEM_NAME system)
cache_value(CMAKE_OSX_SYSROOT sdk)
cache_value(CMAKE_OSX_DEPLOYMENT_TARGET target)
cache_value(PINBALL_IOS_PRESEED applied)
if(NOT system STREQUAL "iOS")
  message(FATAL_ERROR "${BUILD} is not an iOS configuration")
endif()
if(NOT applied STREQUAL "")
  message(FATAL_ERROR "${BUILD} was itself configured from ${applied}: record from a full "
                      "configuration (a new build directory, without -C cmake/IosPreseed.cmake)")
endif()
if(sdk STREQUAL "")
  set(sdk "iphoneos")
endif()
if(NOT sdk MATCHES "^(iphoneos|iphonesimulator)$" OR NOT target MATCHES "^[0-9.]+$")
  message(FATAL_ERROR "unexpected SDK '${sdk}' or deployment target '${target}' in ${cache}")
endif()
pinball_preseed_keys()
if(pinball_preseed_xcode STREQUAL "")
  message(FATAL_ERROR "cannot read Xcode's build version (xcodebuild -version)")
endif()

set(content
"# SDL configure results recorded by cmake/MakeIosPreseed.cmake. Do not edit.
# Applied by cmake/IosPreseed.cmake only when all four keys match.
# xcode: ${pinball_preseed_xcode}
# sdk: ${sdk}
# deployment-target: ${target}
# pinned-deps-sha256: ${pinball_preseed_deps}
")
set(count 0)
list(SORT entries)
foreach(entry IN LISTS entries)
  if(NOT entry MATCHES "^([A-Za-z0-9_]+):INTERNAL=(.*)$")
    continue()
  endif()
  set(name "${CMAKE_MATCH_1}")
  set(value "${CMAKE_MATCH_2}")
  if(name MATCHES "${PINBALL_PRESEED_NAME}")
    if(NOT value MATCHES "^1?$")
      message(FATAL_ERROR "${name} is '${value}', neither 1 nor empty: not a yes-or-no result")
    endif()
    string(APPEND content "${name}=${value}\n")
    math(EXPR count "${count} + 1")
  endif()
endforeach()
if(count LESS 50)
  message(FATAL_ERROR "only ${count} results found in ${cache}: was SDL configured?")
endif()
set(output "${OUTPUT_DIR}/sdl-ios-${pinball_preseed_xcode}-${sdk}.txt")
file(WRITE "${output}" "${content}")
message(STATUS "wrote ${count} results to ${output}")
