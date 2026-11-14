# Whether the iOS icon is still a placeholder. Used by cmake/Ios.cmake, and
# by the test tests/CheckIosIconGuard.cmake, which runs it in script mode.
#
# A placeholder is known two ways, so that neither a forgotten marker nor a
# regenerated placeholder slips through: a marker file beside the catalog,
# and the SHA-256 of every placeholder that was ever committed.
#
# pinball_ios_icon_is_placeholder(<result> ICON <png> MARKER <file> [HASHES <sha256>...])
#   sets <result> to TRUE if MARKER exists or ICON's SHA-256 is one of
#   HASHES (default: the placeholders the repository had), else FALSE.
set(PINBALL_IOS_PLACEHOLDER_ICON_HASHES
  # tools/ios/make_placeholder_icon.py, committed until the real icon came.
  62cae5f0b7bbcca630794c23a725009451cac0e2c6bee2b507655b3ff99709bb)

function(pinball_ios_icon_is_placeholder result)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "ICON;MARKER" "HASHES")
  if(NOT arg_ICON OR NOT arg_MARKER)
    message(FATAL_ERROR "pinball_ios_icon_is_placeholder: ICON and MARKER are required")
  endif()
  if(NOT DEFINED arg_HASHES)
    set(arg_HASHES ${PINBALL_IOS_PLACEHOLDER_ICON_HASHES})
  endif()
  set(placeholder FALSE)
  if(EXISTS "${arg_MARKER}")
    set(placeholder TRUE)
  endif()
  if(NOT EXISTS "${arg_ICON}")
    # No icon at all is no better than a placeholder.
    set(placeholder TRUE)
  else()
    file(SHA256 "${arg_ICON}" hash)
    # list(FIND), not IN_LIST: the module runs under any policy settings.
    list(FIND arg_HASHES "${hash}" found)
    if(NOT found EQUAL -1)
      set(placeholder TRUE)
    endif()
  endif()
  set(${result} ${placeholder} PARENT_SCOPE)
endfunction()
