# Runs TEST with a scratch directory that does not exist beforehand.
# Usage: cmake -DTEST=<exe> -DSCRATCH=<dir> -P RunInScratch.cmake
if(NOT DEFINED TEST OR NOT DEFINED SCRATCH)
  message(FATAL_ERROR "TEST and SCRATCH are required")
endif()
# Only ever remove a directory an earlier run of this test left behind.
if(EXISTS "${SCRATCH}")
  if(NOT SCRATCH MATCHES "/safefile_scratch$")
    message(FATAL_ERROR "refusing to remove ${SCRATCH}")
  endif()
  file(REMOVE_RECURSE "${SCRATCH}")
endif()
execute_process(COMMAND "${TEST}" "${SCRATCH}" RESULT_VARIABLE code TIMEOUT 30)
if(NOT code STREQUAL "0")
  message(FATAL_ERROR "test failed: ${code}")
endif()
