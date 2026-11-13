# Checks the macOS application bundle: Info.plist is well-formed and carries
# the expected identifier, version, names and icon; the icon and the assets
# are in Resources; and the executable is the only thing in Contents/MacOS.
# Usage: cmake -DBUNDLE=<.app> -DEXECUTABLE=<path> -DNAME=<the game's name>
#              -DIDENTIFIER=<id> -DVERSION=<x.y.z> -P CheckBundle.cmake
foreach(var IN ITEMS BUNDLE EXECUTABLE NAME IDENTIFIER VERSION)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "BUNDLE, EXECUTABLE, NAME, IDENTIFIER and VERSION are required")
  endif()
endforeach()

set(plist "${BUNDLE}/Contents/Info.plist")
if(NOT EXISTS "${plist}")
  message(FATAL_ERROR "missing ${plist}")
endif()
execute_process(COMMAND plutil -lint "${plist}" RESULT_VARIABLE code OUTPUT_VARIABLE out
                ERROR_VARIABLE out)
if(NOT code STREQUAL "0")
  message(FATAL_ERROR "plutil -lint failed: ${out}")
endif()

function(expect_key key want)
  execute_process(COMMAND plutil -extract "${key}" raw -o - "${plist}"
                  RESULT_VARIABLE code OUTPUT_VARIABLE got ERROR_VARIABLE err
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT code STREQUAL "0")
    message(FATAL_ERROR "Info.plist has no ${key}: ${err}")
  endif()
  if(NOT got STREQUAL want)
    message(FATAL_ERROR "Info.plist ${key} is '${got}', expected '${want}'")
  endif()
endfunction()

get_filename_component(exe_name "${EXECUTABLE}" NAME)
expect_key(CFBundleIdentifier "${IDENTIFIER}")
expect_key(CFBundleShortVersionString "${VERSION}")
expect_key(CFBundleVersion "${VERSION}")
expect_key(CFBundleName "${NAME}")
expect_key(CFBundleDisplayName "${NAME}")
expect_key(CFBundleExecutable "${exe_name}")
expect_key(CFBundlePackageType "APPL")
expect_key(CFBundleIconFile "AppIcon")
expect_key(NSHighResolutionCapable "true")
expect_key(LSApplicationCategoryType "public.app-category.arcade-games")

execute_process(COMMAND plutil -extract LSMinimumSystemVersion raw -o - "${plist}"
                OUTPUT_VARIABLE min_os OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT min_os MATCHES "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
  message(FATAL_ERROR "Info.plist LSMinimumSystemVersion is '${min_os}'")
endif()

if(NOT EXISTS "${BUNDLE}/Contents/MacOS/${exe_name}")
  message(FATAL_ERROR "the executable is not at Contents/MacOS/${exe_name}")
endif()
if(NOT EXISTS "${BUNDLE}/Contents/Resources/AppIcon.icns")
  message(FATAL_ERROR "the icon is not at Contents/Resources/AppIcon.icns")
endif()
if(NOT IS_DIRECTORY "${BUNDLE}/Contents/Resources/assets")
  message(FATAL_ERROR "the assets are not at Contents/Resources/assets")
endif()

# Nothing but the game belongs beside it: the development tools and the
# tests' scratch files live outside the bundle.
file(GLOB beside LIST_DIRECTORIES true RELATIVE "${BUNDLE}/Contents/MacOS"
     "${BUNDLE}/Contents/MacOS/*")
if(NOT beside STREQUAL exe_name)
  message(FATAL_ERROR "Contents/MacOS holds more than the game: ${beside}")
endif()
