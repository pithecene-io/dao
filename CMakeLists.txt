cmake_minimum_required(VERSION 3.30)
project(dao LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(compiler/frontend)
add_subdirectory(compiler/ir)
add_subdirectory(compiler/backend)
add_subdirectory(compiler/analysis)
add_subdirectory(compiler/driver)
add_subdirectory(tools/playground)
