add_library(dao_runtime STATIC
  io.c
  equality.c
  convert.c
  overflow.c
  generator.c
  resource.c
  string.c
)

# This is C code, not C++.
set_target_properties(dao_runtime PROPERTIES
  LINKER_LANGUAGE C
)

# Allow runtime sources to include dao_abi.h via "dao_abi.h".
target_include_directories(dao_runtime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
