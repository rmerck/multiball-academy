# The rule for an address the game is built with (the privacy policy's,
# PINBALL_PRIVACY_URL): it is compiled into the source as a string and, on a
# touch screen, handed to the system's browser when the player taps the
# button. So it must be a page of this project's own repository on GitHub,
# over https, with a path of plain characters only: nothing that could end
# the C++ string, start a new line, step out of the repository (no "..",
# no query or fragment), or point anywhere else.
# Sets result to TRUE or FALSE.
function(pinball_check_url url result)
  set(ok FALSE)
  string(LENGTH "${url}" length)
  if(length LESS_EQUAL 200
     AND url MATCHES "^https://github\\.com/rmerck/multiball-academy/[A-Za-z0-9._~%/-]*$"
     AND NOT url MATCHES "\\.\\.")
    set(ok TRUE)
  endif()
  set(${result} ${ok} PARENT_SCOPE)
endfunction()
