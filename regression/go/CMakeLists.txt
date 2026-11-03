# The Go front-end needs cbmc-go-helper, which is only built when a Go
# toolchain is available (src/go/CMakeLists.txt).
if(TARGET cbmc-go-helper)
  add_test_pl_tests("$<TARGET_FILE:cbmc>")

  # The capacities of append that the front-end uses for gc
  # (src/go/helper/runtime/rt.go), checked against the Go toolchain, with
  # and without optimisation (which may back slices by stack buffers).
  set(go_helper_dir "${CMAKE_SOURCE_DIR}/src/go/helper")
  add_test(
    NAME go-gc-capacity-CORE
    COMMAND ${CMAKE_COMMAND} -E env GOTOOLCHAIN=local GOFLAGS=-mod=mod
            "${GO_EXECUTABLE}" test -count=1 ./runtime
    WORKING_DIRECTORY "${go_helper_dir}")
  add_test(
    NAME go-gc-capacity-no-optimisation-CORE
    COMMAND ${CMAKE_COMMAND} -E env GOTOOLCHAIN=local GOFLAGS=-mod=mod
            "${GO_EXECUTABLE}" test -count=1 -gcflags=all=-N ./runtime
    WORKING_DIRECTORY "${go_helper_dir}")
  set_tests_properties(
    go-gc-capacity-CORE go-gc-capacity-no-optimisation-CORE
    PROPERTIES LABELS "CORE;CBMC" TIMEOUT 1200)
endif()
