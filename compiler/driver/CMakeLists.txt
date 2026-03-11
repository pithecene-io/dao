add_executable(daoc main.cpp)
target_link_libraries(daoc PRIVATE dao_analysis dao_backend_llvm dao_ir dao_frontend)

# Bake the path to the runtime static library so `daoc build` can link it.
target_compile_definitions(daoc PRIVATE
  DAO_RUNTIME_LIB="$<TARGET_FILE:dao_runtime>"
)
