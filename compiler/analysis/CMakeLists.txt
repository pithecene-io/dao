add_library(dao_analysis STATIC
  goto_definition.cpp
  hover.cpp
  semantic_tokens.cpp
)

target_include_directories(dao_analysis PUBLIC
  ${CMAKE_SOURCE_DIR}/compiler
)

target_link_libraries(dao_analysis PUBLIC dao_frontend)

# Tests
find_package(ut REQUIRED)

add_executable(semantic_tokens_test
  semantic_tokens_test.cpp
)

target_link_libraries(semantic_tokens_test PRIVATE dao_analysis Boost::ut)

target_compile_definitions(semantic_tokens_test PRIVATE
  DAO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(CTest)
add_test(NAME semantic_tokens_test COMMAND semantic_tokens_test)
