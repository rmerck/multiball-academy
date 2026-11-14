# The rule for an address the game is built with (the privacy policy's,
# PINBALL_PRIVACY_URL): it is compiled into the source as a string and, on a
# touch screen, handed to the system's browser when the player taps the
# button. So: https, a host, and a path of plain characters only. Nothing
# that could end the C++ string, start a new line, carry a user name, a
# query or a fragment, or make the system open another application.
# Sets result to TRUE or FALSE.
function(pinball_check_url url result)
  set(ok FALSE)
  string(LENGTH "${url}" length)
  if(length LESS_EQUAL 200
     AND url MATCHES "^https://[a-z0-9]([a-z0-9-]*[a-z0-9])?(\\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)+(/[A-Za-z0-9._~%/-]*)?$")
    set(ok TRUE)
  endif()
  set(${result} ${ok} PARENT_SCOPE)
endfunction()
