cmake_minimum_required(VERSION 3.20)
project(cand VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CAND_TOOLCHAIN_MANIFEST "${CMAKE_SOURCE_DIR}/toolchains/cand1-v1-linux-x86_64.json"
    CACHE FILEPATH "C&1/v1 reference toolchain manifest")
if (NOT EXISTS "${CAND_TOOLCHAIN_MANIFEST}")
    message(FATAL_ERROR "C&1 toolchain manifest not found: ${CAND_TOOLCHAIN_MANIFEST}")
endif()
file(READ "${CAND_TOOLCHAIN_MANIFEST}" CAND_TOOLCHAIN_JSON)
string(JSON CAND_SUPPORTED_CLANG GET "${CAND_TOOLCHAIN_JSON}" analyzer clang_version)
string(JSON CAND_SUPPORTED_LLVM GET "${CAND_TOOLCHAIN_JSON}" analyzer llvm_version)
string(JSON CAND_SUPPORTED_TARGET GET "${CAND_TOOLCHAIN_JSON}" target)
string(JSON CAND_SUPPORTED_STANDARD GET "${CAND_TOOLCHAIN_JSON}" language standard)
string(JSON CAND_SUPPORTED_SYSROOT GET "${CAND_TOOLCHAIN_JSON}" sysroot identity)
string(JSON CAND_SUPPORTED_HOST GET "${CAND_TOOLCHAIN_JSON}" host os)
string(JSON CAND_SUPPORTED_ARCH GET "${CAND_TOOLCHAIN_JSON}" host architecture)
string(JSON CAND_SUPPORTED_CONTAINER GET "${CAND_TOOLCHAIN_JSON}" host container)
string(JSON CAND_SUPPORTED_CMAKE GET "${CAND_TOOLCHAIN_JSON}" build cmake)
string(JSON CAND_SUPPORTED_NINJA GET "${CAND_TOOLCHAIN_JSON}" build ninja)
file(SHA256 "${CAND_TOOLCHAIN_MANIFEST}" CAND_TOOLCHAIN_ENVIRONMENT_DIGEST)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "C&1 verifier must be built with Clang; got ${CMAKE_CXX_COMPILER_ID}")
endif()
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    message(FATAL_ERROR "C&1/v1 supports only Linux x86_64; got ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()
file(READ "/etc/os-release" CAND_OS_RELEASE ERROR_QUIET)
if (NOT CAND_OS_RELEASE MATCHES "ID=ubuntu" OR
    NOT CAND_OS_RELEASE MATCHES "VERSION_ID=\"24\\.04\"")
    message(FATAL_ERROR "C&1/v1 requires Ubuntu 24.04 reference environment")
endif()
if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL CAND_SUPPORTED_CLANG)
    message(FATAL_ERROR "unsupported verifier build compiler ${CMAKE_CXX_COMPILER_VERSION}; expected ${CAND_SUPPORTED_CLANG}")
endif()
if (NOT LLVM_PACKAGE_VERSION VERSION_EQUAL CAND_SUPPORTED_LLVM)
    message(FATAL_ERROR "unsupported LLVM ${LLVM_PACKAGE_VERSION}; expected ${CAND_SUPPORTED_LLVM}")
endif()
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE CAND_BUILD_TARGET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT CAND_BUILD_TARGET STREQUAL CAND_SUPPORTED_TARGET)
    message(FATAL_ERROR "unsupported build target ${CAND_BUILD_TARGET}; expected ${CAND_SUPPORTED_TARGET}")
endif()
if (NOT CMAKE_VERSION VERSION_EQUAL CAND_SUPPORTED_CMAKE)
    message(FATAL_ERROR "unsupported CMake ${CMAKE_VERSION}; expected ${CAND_SUPPORTED_CMAKE}")
endif()
execute_process(
    COMMAND ninja --version
    OUTPUT_VARIABLE CAND_BUILD_NINJA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT CAND_BUILD_NINJA STREQUAL CAND_SUPPORTED_NINJA)
    message(FATAL_ERROR "unsupported Ninja ${CAND_BUILD_NINJA}; expected ${CAND_SUPPORTED_NINJA}")
endif()

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
    CAND_TOOLCHAIN_CLANG_VERSION="${CAND_SUPPORTED_CLANG}"
    CAND_TOOLCHAIN_LLVM_VERSION="${CAND_SUPPORTED_LLVM}"
    CAND_TOOLCHAIN_TARGET="${CAND_SUPPORTED_TARGET}"
    CAND_TOOLCHAIN_STANDARD="${CAND_SUPPORTED_STANDARD}"
    CAND_TOOLCHAIN_SYSROOT="${CAND_SUPPORTED_SYSROOT}"
    CAND_TOOLCHAIN_HOST="${CAND_SUPPORTED_HOST}"
    CAND_TOOLCHAIN_ARCH="${CAND_SUPPORTED_ARCH}"
    CAND_TOOLCHAIN_CONTAINER="${CAND_SUPPORTED_CONTAINER}"
    CAND_TOOLCHAIN_ENVIRONMENT_DIGEST="${CAND_TOOLCHAIN_ENVIRONMENT_DIGEST}"
    CAND_BUILD_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    CAND_BUILD_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
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
add_test(
    NAME cand-p2-borrow-lifetimes
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/p2/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand1-a-profile-heap
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/cand1/heap/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand1-b-transport-boundary
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/cand1/transport/run.sh $<TARGET_FILE:cand>
)
add_test(
    NAME cand1-c-conformance
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/cand1/conformance/run.py $<TARGET_FILE:cand>
)
add_test(
    NAME cand1-c-fast-fuzz
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/fuzz/runner.py
        --cand $<TARGET_FILE:cand>
        --mode fast
        --seed 12345
        --output ${CMAKE_BINARY_DIR}/cand1-c-fast
)
add_test(
    NAME cand1-d-toolchain-compatibility
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/toolchain/run.sh $<TARGET_FILE:cand>
)
