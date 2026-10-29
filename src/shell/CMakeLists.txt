file(GLOB_RECURSE sources "*.cpp" "*.h")

add_library(shell-frontend ${sources})

generic_includes(shell-frontend)

target_link_libraries(shell-frontend util json langapi big-int)
