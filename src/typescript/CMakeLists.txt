add_library(typescript
  typescript_language.cpp
)

generic_includes(typescript)

target_link_libraries(typescript
  util
  langapi
  json
  goto-programs
)
