add_library(dao_runtime STATIC
  dao_runtime.c
)

# This is C code, not C++.
set_target_properties(dao_runtime PROPERTIES
  LINKER_LANGUAGE C
)
