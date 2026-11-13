# What a file of recorded SDL configure results is valid for. Shared by
# cmake/IosPreseed.cmake, which applies one, and cmake/MakeIosPreseed.cmake,
# which writes one. Sets, in the caller's scope:
#   pinball_preseed_xcode    Xcode's build version, e.g. 17F113 (it fixes the
#                            compiler and the SDKs), or "" if it cannot be read
#   pinball_preseed_deps     SHA-256 of cmake/PinnedDeps.cmake, which holds the
#                            SDL version, its hash and the options SDL is
#                            built with
# The other two keys, the SDK name and the deployment target, are the
# caller's to supply: one reads them from the command line, the other from
# a finished cache.
macro(pinball_preseed_keys)
  set(pinball_preseed_xcode "")
  execute_process(COMMAND xcodebuild -version OUTPUT_VARIABLE pinball_preseed_text
                  ERROR_QUIET RESULT_VARIABLE pinball_preseed_code)
  if(pinball_preseed_code EQUAL 0
     AND pinball_preseed_text MATCHES "Build version ([0-9A-Za-z]+)")
    set(pinball_preseed_xcode "${CMAKE_MATCH_1}")
  endif()
  file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/PinnedDeps.cmake" pinball_preseed_deps)
endmacro()

# The only entries a preseed file may hold: results of SDL's yes-or-no
# questions about the compiler and the C library, each "1" or empty. Nothing
# that names a path, a URL, a hash or an option of this project, of SDL or of
# FetchContent can be written or applied through one.
set(PINBALL_PRESEED_NAME
  "^(LIBC_HAS_|LIBC_IS|HAVE_|COMPILER_SUPPORTS_|COMPILER_HAS_|LINKER_SUPPORTS_)[A-Za-z0-9_]+$")
