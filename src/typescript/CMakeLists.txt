add_library(typescript
  typescript_language.cpp
  typescript_converter.cpp
)

generic_includes(typescript)

target_link_libraries(typescript
  util
  langapi
  json
  goto-programs
)
