file(GLOB sources "*.cpp" "*.h")

add_library(go-frontend ${sources})

generic_includes(go-frontend)

target_link_libraries(go-frontend util json langapi linking)

# cbmc-go-helper: parses and type-checks Go with go/parser and go/types
# (helper/main.go). It needs a Go toolchain at build time; the Go front-end
# reports a clear error at run time if the helper is missing.
find_program(GO_EXECUTABLE go)
if(GO_EXECUTABLE)
  set(go_helper "${CMAKE_BINARY_DIR}/bin/cbmc-go-helper")
  add_custom_command(
    OUTPUT "${go_helper}"
    COMMAND ${CMAKE_COMMAND} -E env GOTOOLCHAIN=local GOFLAGS=-mod=mod
            "${GO_EXECUTABLE}" build -trimpath -o "${go_helper}" .
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/helper"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/helper/main.go"
      "${CMAKE_CURRENT_SOURCE_DIR}/helper/go.mod"
      "${CMAKE_CURRENT_SOURCE_DIR}/helper/cprover/cprover.go"
    COMMENT "Building cbmc-go-helper")
  add_custom_target(cbmc-go-helper ALL DEPENDS "${go_helper}")
else()
  message(STATUS "Go toolchain not found: cbmc-go-helper will not be built")
endif()
