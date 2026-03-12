cmake_minimum_required(VERSION 3.16)

project(compiler_lab)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place built executables in build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find LLVM
# Find LLVM
if(NOT LLVM_DIR OR LLVM_DIR STREQUAL "LLVM_DIR-NOTFOUND")
    set(LLVM_DIR "${CMAKE_SOURCE_DIR}/../llvm/build/lib/cmake/llvm")
endif()

message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

if(EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    message(STATUS "LLVMConfig.cmake found at: ${LLVM_DIR}")
else()
    message(FATAL_ERROR "LLVMConfig.cmake NOT found at: ${LLVM_DIR}")
endif()

find_package(ZLIB REQUIRED)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

set(MAIN_FILE src/main.cpp)

# Automatically collect all source files from src/
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SOURCES ${MAIN_FILE})
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/parser_test.cpp)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Define the executable name (MUST be 'c4' per requirements)
add_executable(c4 ${MAIN_FILE} ${SOURCES})

# Optional: organize IDEs and editors
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${MAIN_FILE} ${SOURCES})
target_include_directories(c4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
llvm_map_components_to_libnames(LLVM_LIBS core support irreader)
target_link_libraries(c4 ${LLVM_LIBS})
