add_library(dao_ir STATIC
  hir/hir.cpp
  hir/hir_builder.cpp
  hir/hir_printer.cpp
)

target_include_directories(dao_ir PUBLIC
  ${CMAKE_SOURCE_DIR}/compiler
)

target_link_libraries(dao_ir PUBLIC dao_frontend)

# Tests
find_package(ut REQUIRED)

add_executable(hir_test
  hir/hir_test.cpp
)

target_link_libraries(hir_test PRIVATE dao_ir Boost::ut)

target_compile_definitions(hir_test PRIVATE
  DAO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(CTest)
add_test(NAME hir_test COMMAND hir_test)
