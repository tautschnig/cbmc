# Start the TypeScript parse daemon before running tests and stop it
# after. The daemon caches the TypeScript compiler across invocations
# via the Language Service, giving an ~18x speedup on the full
# regression suite (measured 2026-05-11).
#
# CBMC checks the CBMC_TS_SERVER_SOCKET environment variable and
# connects to the daemon if set. With the variable unset (e.g. when
# running one-off tests manually), CBMC falls back to one-shot node
# invocation.

set(ts_server_script
    "${CMAKE_SOURCE_DIR}/scripts/cbmc_ts_server")

# Compute the socket path once: scripts/cbmc_ts_server socket-path
# is deterministic (based on $XDG_RUNTIME_DIR and $USER) but we
# execute it at configure time so ctest knows where the daemon lives.
execute_process(
    COMMAND "${ts_server_script}" socket-path
    OUTPUT_VARIABLE ts_server_socket_raw
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(ts_server_socket "${ts_server_socket_raw}")

add_test(
    NAME "cbmc_ts_server_start"
    COMMAND "${ts_server_script}" start
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties("cbmc_ts_server_start" PROPERTIES
    FIXTURES_SETUP TS_SERVER
    LABELS "CBMC;TS_SERVER"
    TIMEOUT 60)

add_test(
    NAME "cbmc_ts_server_stop"
    COMMAND "${ts_server_script}" stop
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties("cbmc_ts_server_stop" PROPERTIES
    FIXTURES_CLEANUP TS_SERVER
    LABELS "CBMC;TS_SERVER"
    TIMEOUT 60)

# Add the regression tests and wire them to the fixture.
add_test_pl_tests("${CMAKE_BINARY_DIR}/bin/cbmc")

# Each of the CORE / THOROUGH / FUTURE / KNOWNBUG tests needs the
# daemon. Require the fixture and set the socket path via ENVIRONMENT.
foreach(profile CORE THOROUGH FUTURE KNOWNBUG)
    set(test_name "typescript-${profile}")
    if(TEST "${test_name}")
        set_tests_properties("${test_name}" PROPERTIES
            FIXTURES_REQUIRED TS_SERVER
            ENVIRONMENT "CBMC_TS_SERVER_SOCKET=${ts_server_socket}")
    endif()
endforeach()
