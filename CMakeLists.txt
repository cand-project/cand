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

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

execute_process(
    COMMAND git rev-parse --verify HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CAND_VERIFIER_SOURCE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (CAND_VERIFIER_SOURCE_COMMIT STREQUAL "")
    set(CAND_VERIFIER_SOURCE_COMMIT "unknown")
endif()

add_executable(cand
    src/cand.cpp
    src/agent_policy.cpp
)

# Use the distribution's monolithic shared Clang and LLVM libraries. This keeps
# one LLVM runtime/command-line registry in the process and avoids duplicate
# global option registration from split static component libraries.
target_link_libraries(cand PRIVATE clang-cpp LLVM)
target_compile_definitions(cand PRIVATE
    CAND_VERIFIER_SOURCE_COMMIT="${CAND_VERIFIER_SOURCE_COMMIT}"
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cand PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(
    NAME cand-p0-proof
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/p0/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-showcase
    COMMAND bash ${CMAKE_SOURCE_DIR}/examples/showcase/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p0-1-failclosed
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/failclosed/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p0-2-cfg
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/cfg/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-differential-asan
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/differential/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p0-3-storage
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/storage/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p0-4-interprocedural
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/interprocedural/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p0-5-agent-verification
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/agent/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand-p1-explicit-ownership
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/p1/run.sh $<TARGET_FILE:cand>
)
