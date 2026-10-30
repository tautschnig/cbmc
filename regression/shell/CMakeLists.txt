# The shell front-end parses with shfmt (https://github.com/mvdan/sh).
# Use $CBMC_SHFMT if set at configure time, else shfmt on the PATH; skip
# the tests if neither is available.
if(DEFINED ENV{CBMC_SHFMT})
  set(SHFMT_EXECUTABLE "$ENV{CBMC_SHFMT}")
else()
  find_program(SHFMT_EXECUTABLE shfmt)
endif()

if(SHFMT_EXECUTABLE)
  add_test_pl_tests("${CMAKE_BINARY_DIR}/bin/cbmc")
  foreach(profile CORE THOROUGH FUTURE KNOWNBUG)
    set_property(TEST "shell-${profile}" APPEND PROPERTY
      ENVIRONMENT "CBMC_SHFMT=${SHFMT_EXECUTABLE}")
  endforeach()
else()
  message(STATUS "shfmt not found: skipping regression/shell")
endif()
