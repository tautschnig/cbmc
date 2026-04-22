file(GLOB_RECURSE sources "*.cpp" "*.h")

# Exclude the scripts directory from C++ compilation
list(FILTER sources EXCLUDE REGEX "scripts/")

add_library(python-frontend ${sources})

generic_includes(python-frontend)

target_link_libraries(python-frontend util json langapi)
