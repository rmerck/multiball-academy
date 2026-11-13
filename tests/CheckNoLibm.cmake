# The table's layout must not be computed with the C library's mathematics:
# its last bit differs between libraries, and with it the ball's path.
# Fails if the header includes <cmath> or names one of its functions.
#   cmake -DHEADER=<path to TableLayout.hpp> -P CheckNoLibm.cmake
if(NOT DEFINED HEADER OR NOT EXISTS "${HEADER}")
  message(FATAL_ERROR "CheckNoLibm: no such header: ${HEADER}")
endif()
file(READ "${HEADER}" text)
# Comments may say what they like.
string(REGEX REPLACE "//[^\n]*" "" code "${text}")
if(code MATCHES "#[ \t]*include[ \t]*<(cmath|math\\.h|complex|valarray)>")
  message(FATAL_ERROR "${HEADER} includes ${CMAKE_MATCH_1}")
endif()
if(code MATCHES "(std::|[^A-Za-z_])(sin|cos|tan|asin|acos|atan|atan2|hypot|pow|exp|log|sqrt|cbrt)f?[ \t]*\\(")
  message(FATAL_ERROR "${HEADER} calls ${CMAKE_MATCH_2}")
endif()
message(STATUS "no library mathematics in ${HEADER}")
