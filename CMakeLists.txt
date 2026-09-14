cmake_minimum_required(VERSION 3.20)
project(cand VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(cand
    src/cand.cpp
)

target_link_libraries(cand PRIVATE
    clangTooling
    clangFrontend
    clangAST
    clangLex
    clangBasic
    LLVMSupport
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cand PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(
    NAME cand-p0-proof
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/p0/run.sh $<TARGET_FILE:cand>
)
