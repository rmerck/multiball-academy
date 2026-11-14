# The rule for the addresses the game is built with (cmake/ProductUrl.cmake):
# what it accepts and what it refuses.
# Usage: cmake -DSOURCE_DIR=<repository> -P CheckProductUrl.cmake
cmake_minimum_required(VERSION 3.26)
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
include("${SOURCE_DIR}/cmake/ProductUrl.cmake")

set(good
  "https://github.com/rmerck/multiball-academy/blob/main/docs/PRIVACY.md"
  "https://example.org/"
  "https://example.org/a-b_c.d~e/f%20g")
set(bad
  ""
  "http://github.com/rmerck/multiball-academy"
  "https://"
  "https:///path"
  "javascript:alert(1)"
  "file:///etc/passwd"
  "https://user@example.org/"
  "https://example.org/\"quote"
  "https://example.org/a b"
  "https://example.org/back\\\\slash"
  "https://example.org/?q=1"
  "https://example.org/#frag"
  "HTTPS://EXAMPLE.ORG/")
foreach(url IN LISTS good)
  pinball_check_url("${url}" ok)
  if(NOT ok)
    message(FATAL_ERROR "refused, but should pass: '${url}'")
  endif()
endforeach()
foreach(url IN LISTS bad)
  pinball_check_url("${url}" ok)
  if(ok)
    message(FATAL_ERROR "accepted, but should be refused: '${url}'")
  endif()
endforeach()
# A line break, which a list cannot hold.
pinball_check_url("https://example.org/\nx" ok)
if(ok)
  message(FATAL_ERROR "accepted a line break")
endif()
message(STATUS "product URL rule: all cases as expected")
