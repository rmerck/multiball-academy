# The ball's path must not depend on the C library's transcendental
# functions: their last bit differs between libraries, and with it every
# trajectory after. Two passes:
#
#   HEADER   the table's layout. No <cmath> at all and none of its functions,
#            sqrt included: every table in it is literal.
#   SOURCES  (comma separated) the files that move the ball. <cmath> is
#            allowed for what IEEE 754 rounds exactly (sqrt, fabs, floor,
#            ceil, round); sin, cos, tan and their inverses, the hyperbolic
#            ones, hypot, pow, cbrt, exp and log, with or without an f or l
#            suffix and std:: or not, are not. Angles go through Box2D's own
#            b2ComputeCosSin and b2MakeRot, which are plain arithmetic.
#
#   cmake -DHEADER=<TableLayout.hpp> -DSOURCES=<a.cpp,b.hpp,...> -P CheckNoLibm.cmake
#
# The script first proves itself on lines made up here: each banned name
# must be caught, and what is allowed must pass.
set(banned "sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|hypot|pow|cbrt")
set(banned "${banned}|exp|exp2|expm1|log|log2|log10|log1p")
set(exact "sqrt")

# Sets ${out} to what is wrong with the text, or to nothing.
function(find_library_mathematics text strict out)
  # Comments may say what they like.
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  set(names "${banned}")
  if(strict)
    set(names "${banned}|${exact}")
    if(code MATCHES "#[ \t]*include[ \t]*<(cmath|math\\.h|complex|valarray)>")
      set(${out} "includes <${CMAKE_MATCH_1}>" PARENT_SCOPE)
      return()
    endif()
  endif()
  if(code MATCHES "(^|[^A-Za-z0-9_])(${names})[fl]?[ \t\n]*\\(")
    set(${out} "calls ${CMAKE_MATCH_2}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

foreach(line IN ITEMS
    "float a = std::sin(x)" "float a = sin (x)" "a = ::cosf(x)" "a=tanl(x)" "return std::atan2(y, x)"
    "x = std::hypot(a, b)" "x = powf(a, 2.0F)" "x = std::exp(a)" "x = std::log(a)" "x = log10f(a)"
    "x = std::tanh(a)" "x = std::cbrt(a)" "x = std::exp2(a)" "x = sin\n  (a)" "x=-acos(a)")
  find_library_mathematics("#include <cmath>\n${line};\n" FALSE found)
  if(found STREQUAL "")
    message(FATAL_ERROR "CheckNoLibm does not catch: ${line}")
  endif()
endforeach()
foreach(line IN ITEMS
    "float a = std::sqrt(x)" "a = std::fabs(x) + std::floor(y)" "b2CosSin cs = b2ComputeCosSin(a)"
    "// the angle, atan2(y, x), is not worked out here" "float basin(float x)" "x = catalog(a)"
    "explicit Thing(int a)" "x = cosine(a)" "x = b2Atan2(y, x)")
  find_library_mathematics("#include <cmath>\n${line};\n" FALSE found)
  if(NOT found STREQUAL "")
    message(FATAL_ERROR "CheckNoLibm wrongly refuses: ${line} (${found})")
  endif()
endforeach()
find_library_mathematics("#include <cmath>\n" TRUE found)
if(found STREQUAL "")
  message(FATAL_ERROR "CheckNoLibm does not catch <cmath> in the layout")
endif()
find_library_mathematics("float a = std::sqrt(2.0F);\n" TRUE found)
if(found STREQUAL "")
  message(FATAL_ERROR "CheckNoLibm does not catch sqrt in the layout")
endif()

if(NOT DEFINED HEADER OR NOT EXISTS "${HEADER}")
  message(FATAL_ERROR "CheckNoLibm: no such header: ${HEADER}")
endif()
file(READ "${HEADER}" text)
find_library_mathematics("${text}" TRUE found)
if(NOT found STREQUAL "")
  message(FATAL_ERROR "${HEADER} ${found}")
endif()
message(STATUS "no library mathematics in ${HEADER}")

if(NOT DEFINED SOURCES OR SOURCES STREQUAL "")
  message(FATAL_ERROR "CheckNoLibm: no SOURCES given")
endif()
string(REPLACE "," ";" sources "${SOURCES}")
foreach(source IN LISTS sources)
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "CheckNoLibm: no such file: ${source}")
  endif()
  file(READ "${source}" text)
  find_library_mathematics("${text}" FALSE found)
  if(NOT found STREQUAL "")
    message(FATAL_ERROR "${source} ${found}")
  endif()
  message(STATUS "no transcendental functions in ${source}")
endforeach()
