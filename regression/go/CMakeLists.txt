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

  # The same for TinyGo (rt.GrowCapTinyGo), if it is installed.
  find_program(TINYGO_EXECUTABLE tinygo)
  if(TINYGO_EXECUTABLE)
    get_filename_component(go_bin_dir "${GO_EXECUTABLE}" DIRECTORY)
    foreach(opt z 0)
      add_test(
        NAME go-tinygo-capacity-opt-${opt}-CORE
        COMMAND ${CMAKE_COMMAND} -E env GOTOOLCHAIN=local GOFLAGS=-mod=mod
                "PATH=${go_bin_dir}:$ENV{PATH}"
                "${TINYGO_EXECUTABLE}" test -opt=${opt} ./runtime
        WORKING_DIRECTORY "${go_helper_dir}")
      set_tests_properties(go-tinygo-capacity-opt-${opt}-CORE
        PROPERTIES LABELS "CORE;CBMC" TIMEOUT 1200)
    endforeach()
  endif()
endif()
