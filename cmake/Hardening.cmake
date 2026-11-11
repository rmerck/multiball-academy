# Warning and hardening flags shared by every target.
function(pinball_apply_hardening target scope)
  if(MSVC)
    target_compile_options(${target} ${scope} /W4 /permissive- /sdl)
    if(PINBALL_WERROR)
      target_compile_options(${target} ${scope} /WX)
    endif()
    return()
  endif()

  target_compile_options(${target} ${scope}
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
    -Wnull-dereference -Wformat=2 -Wimplicit-fallthrough
    -fstack-protector-strong)
  if(PINBALL_WERROR)
    target_compile_options(${target} ${scope} -Werror)
  endif()
  if(PINBALL_SANITIZE)
    target_compile_options(${target} ${scope}
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} ${scope} -fsanitize=address,undefined)
  endif()
endfunction()
