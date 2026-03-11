find_package(httplib REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(dao_playground
  main.cpp
  analyze.cpp
  examples.cpp
)

target_link_libraries(dao_playground PRIVATE
  dao_analysis
  dao_backend_llvm
  dao_ir
  dao_frontend
  httplib::httplib
  nlohmann_json::nlohmann_json
)

target_compile_definitions(dao_playground PRIVATE
  DAO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
