add_library(typescript
  typescript_language.cpp
  typescript_converter.cpp
  typescript_converter_call.cpp
  typescript_converter_stmt.cpp
  typescript_converter_func.cpp
  typescript_regex.cpp
  expr2typescript.cpp
)

generic_includes(typescript)

target_link_libraries(typescript
  util
  langapi
  json
  goto-programs
)
