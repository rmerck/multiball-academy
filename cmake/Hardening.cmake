# Warning and hardening flags shared by every target.
# Never add -ffast-math here: core code relies on std::isfinite to reject NaN
# and infinite input, and fast-math lets the compiler delete those checks.
function(pinball_apply_hardening target scope)
  if(MSVC)
    # Control-flow guard, and the loader protections made explicit.
    target_compile_options(${target} ${scope} /W4 /permissive- /sdl /guard:cf)
    target_link_options(${target} ${scope} /guard:cf /DYNAMICBASE /NXCOMPAT)
    if(PINBALL_WERROR)
      target_compile_options(${target} ${scope} /WX)
    endif()
    if(PINBALL_SANITIZE)
      message(FATAL_ERROR "PINBALL_SANITIZE is not supported with MSVC in this project")
    endif()
    return()
  endif()

  target_compile_options(${target} ${scope}
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
    -Wnull-dereference -Wformat=2 -Wimplicit-fallthrough
    -Wdouble-promotion -Wextra-semi -Wunused-macros
    -fstack-protector-strong)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Known to GCC only (clang 18 rejects both names).
    # Not -Wuseless-cast: a cast between size_t and a fixed-width type is
    # needed on one platform and "useless" on another.
    target_compile_options(${target} ${scope} -Wformat-signedness)
  endif()
  # Keep the builder's directory names out of assertion messages and debug
  # information: file names are recorded relative to the source tree. (On
  # macOS a build with debug information still lists its object files by
  # full path; Release builds, which are what is handed out, do not.)
  target_compile_options(${target} ${scope}
    -ffile-prefix-map=${CMAKE_BINARY_DIR}=build
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
  # Bounds and precondition checks in the standard library.
  target_compile_definitions(${target} ${scope}
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST
    _GLIBCXX_ASSERTIONS)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(${target} ${scope} -fstack-clash-protection)
    target_link_options(${target} ${scope} -Wl,-z,relro,-z,now)
    if(NOT PINBALL_SANITIZE)
      # Not in a Debug build: the checks need the optimiser, and without it
      # the definition silently does nothing.
      target_compile_options(${target} ${scope}
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
    endif()
    # Control-flow protection where the compiler and processor have it.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-fcf-protection=full PINBALL_HAS_CF_PROTECTION)
    if(PINBALL_HAS_CF_PROTECTION)
      target_compile_options(${target} ${scope} -fcf-protection=full)
    endif()
    # The arm64 counterpart.
    check_cxx_compiler_flag(-mbranch-protection=standard PINBALL_HAS_BRANCH_PROTECTION)
    if(PINBALL_HAS_BRANCH_PROTECTION)
      target_compile_options(${target} ${scope} -mbranch-protection=standard)
    endif()
  endif()
  if(PINBALL_IOS AND NOT PINBALL_SANITIZE)
    # Fortified library calls. Apple's headers turn level 2 on by themselves
    # in an optimised build; said here so that it does not rest on a default.
    target_compile_options(${target} ${scope}
      $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
      $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
  endif()
  if(PINBALL_WERROR)
    target_compile_options(${target} ${scope} -Werror)
  endif()
  if(PINBALL_SANITIZE)
    target_compile_options(${target} ${scope}
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} ${scope} -fsanitize=address,undefined)
  endif()
endfunction()

# The protective flags alone, without the project's warning settings, which
# dependencies built from source must not inherit. They are set on the
# directory that calls this, so they reach every target made there or below
# it afterwards. cmake/PinnedDeps.cmake is included from the top directory:
# that means the dependencies and the game's own targets alike. The game's
# targets get the same flags again from pinball_apply_hardening(), which
# changes nothing.
function(pinball_dependency_hardening)
  if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    return()
  endif()
  add_compile_options(-fstack-protector-strong
    -ffile-prefix-map=${CMAKE_BINARY_DIR}=build
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_options(-fstack-clash-protection)
    add_link_options(-Wl,-z,relro,-z,now)
    if(NOT PINBALL_SANITIZE)
      # Not in a Debug build, where it does nothing (see above).
      add_compile_options(
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
    endif()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-fcf-protection=full PINBALL_HAS_CF_PROTECTION)
    if(PINBALL_HAS_CF_PROTECTION)
      add_compile_options(-fcf-protection=full)
    endif()
    check_cxx_compiler_flag(-mbranch-protection=standard PINBALL_HAS_BRANCH_PROTECTION)
    if(PINBALL_HAS_BRANCH_PROTECTION)
      add_compile_options(-mbranch-protection=standard)
    endif()
  endif()
  if(PINBALL_IOS AND NOT PINBALL_SANITIZE)
    # As for the game's own code above.
    add_compile_options(
      $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
      $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
  endif()
endfunction()
