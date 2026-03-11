add_executable(daoc main.cpp)
target_link_libraries(daoc PRIVATE dao_analysis dao_backend_llvm dao_ir dao_frontend)
