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
    -fstack-protector-strong)
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
      target_compile_options(${target} ${scope} -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3)
    endif()
    # Control-flow protection where the compiler and processor have it.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-fcf-protection=full PINBALL_HAS_CF_PROTECTION)
    if(PINBALL_HAS_CF_PROTECTION)
      target_compile_options(${target} ${scope} -fcf-protection=full)
    endif()
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
