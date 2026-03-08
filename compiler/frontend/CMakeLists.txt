add_library(dao_frontend STATIC
  lexer/lexer.cpp
)

target_include_directories(dao_frontend PUBLIC
  ${CMAKE_SOURCE_DIR}/compiler
)

# Tests
find_package(ut REQUIRED)

add_executable(lexer_test
  lexer/lexer_test.cpp
)

target_link_libraries(lexer_test PRIVATE dao_frontend Boost::ut)

target_compile_definitions(lexer_test PRIVATE
  DAO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(CTest)
add_test(NAME lexer_test COMMAND lexer_test)
