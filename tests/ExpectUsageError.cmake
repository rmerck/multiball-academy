# Runs PINBALL with ARGS (a |-separated list) and requires a clean rejection:
# exit code exactly 1, the usage text on stderr, and no sanitizer report.
# Usage: cmake -DPINBALL=<path> -DARGS=<a|b> -P ExpectUsageError.cmake
if(NOT DEFINED PINBALL OR NOT DEFINED ARGS)
  message(FATAL_ERROR "PINBALL and ARGS are required")
endif()
string(REPLACE "|" ";" arg_list "${ARGS}")

set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_AUDIODRIVER} dummy)
execute_process(
  COMMAND "${PINBALL}" ${arg_list}
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  TIMEOUT 20)

if(NOT exit_code STREQUAL "1")
  message(FATAL_ERROR "expected exit code 1, got '${exit_code}'\n${out}\n${err}")
endif()
if(NOT err MATCHES "Usage: pinball")
  message(FATAL_ERROR "usage text missing\n${out}\n${err}")
endif()
if("${out}${err}" MATCHES "Sanitizer|runtime error")
  message(FATAL_ERROR "sanitizer report during rejection\n${out}\n${err}")
endif()
