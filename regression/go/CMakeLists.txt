# The Go front-end needs cbmc-go-helper, which is only built when a Go
# toolchain is available (src/go/CMakeLists.txt).
if(TARGET cbmc-go-helper)
  add_test_pl_tests("$<TARGET_FILE:cbmc>")
endif()
