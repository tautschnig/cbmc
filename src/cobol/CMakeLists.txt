file(GLOB_RECURSE sources "*.cpp" "*.h")

add_library(cobol ${sources})

generic_includes(cobol)

target_link_libraries(cobol util goto-programs langapi linking)
