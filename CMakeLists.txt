cmake_minimum_required(VERSION 3.16)

project(compiler_lab)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM
find_package(LLVM 21 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Place built executables in build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MAIN_FILE src/main.cpp)

# Automatically collect all source files from src/
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SOURCES ${MAIN_FILE})
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/parser_test.cpp)

# Define the executable name (MUST be 'c4' per requirements)
add_executable(c4 ${MAIN_FILE} ${SOURCES})

# LLVM includes/defs/libs
target_include_directories(c4 PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(c4 PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(c4 PRIVATE LLVMCore LLVMSupport)

# Optional: organize IDEs and editors
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${MAIN_FILE} ${SOURCES})
