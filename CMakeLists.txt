cmake_minimum_required(VERSION 3.20)
project(cand VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CAND_TOOLCHAIN_MANIFEST "${CMAKE_SOURCE_DIR}/toolchains/cand1-v1-linux-x86_64.json"
    CACHE FILEPATH "C&1/v1 reference toolchain manifest")
set(CAND_TOOLCHAIN_CANONICAL_MANIFEST "${CMAKE_SOURCE_DIR}/toolchains/cand1-v1-linux-x86_64.json")
if (NOT CAND_TOOLCHAIN_MANIFEST STREQUAL CAND_TOOLCHAIN_CANONICAL_MANIFEST)
    message(FATAL_ERROR "C&1 toolchain manifest path is repository-authoritative")
endif()
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
set(CAND_SUPPORTED_NINJA_PATH "/usr/bin/ninja")
file(SHA256 "${CAND_TOOLCHAIN_MANIFEST}" CAND_TOOLCHAIN_ENVIRONMENT_DIGEST)
if (NOT CAND_TOOLCHAIN_ENVIRONMENT_DIGEST STREQUAL
    "6595b5d73b1c7404eee03c2a293015eeae0458cb6fef575c3d5c1e0816391bd5")
    message(FATAL_ERROR "C&1 toolchain manifest digest is not trusted")
endif()

if (DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
    message(FATAL_ERROR "C&1/v1 requires the default sysroot")
endif()
if (DEFINED CMAKE_CXX_COMPILER_TARGET AND NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "")
    message(FATAL_ERROR "C&1/v1 does not permit a compiler target override")
endif()
foreach(variable IN ITEMS
    CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_C_LINKER_LAUNCHER CMAKE_CXX_LINKER_LAUNCHER
    CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN)
    if (DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "C&1/v1 does not permit compiler or linker indirection in ${variable}")
    endif()
endforeach()
foreach(variable IN ITEMS CLANG_CONFIG_FILE CCC_OVERRIDE_OPTIONS)
    if (DEFINED ENV{${variable}} AND NOT "$ENV{${variable}}" STREQUAL "")
        message(FATAL_ERROR "C&1/v1 does not permit frontend configuration through ${variable}")
    endif()
endforeach()
set(CAND_FORBIDDEN_FLAG_VARIABLES
    CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE
    CMAKE_EXE_LINKER_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
    CMAKE_SHARED_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_RELEASE
    CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO)
foreach(flag IN LISTS CAND_FORBIDDEN_FLAG_VARIABLES)
    if (DEFINED ${flag} AND "${${flag}}" MATCHES
        "(^|[ ;])(-isysroot|--sysroot|-target|--target|-L|-B|--gcc-toolchain|-gcc-toolchain|-nostdinc|-nostdlibinc|-fuse-ld|-Xlinker|-Wl,)([=, ;]|$)")
        message(FATAL_ERROR "C&1/v1 does not permit toolchain substitution flags in ${flag}")
    endif()
endforeach()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

get_filename_component(CAND_CXX_COMPILER_REALPATH "${CMAKE_CXX_COMPILER}" REALPATH)
if (NOT CAND_CXX_COMPILER_REALPATH STREQUAL "/usr/lib/llvm-18/bin/clang")
    message(FATAL_ERROR "C&1 verifier compiler path is not /usr/lib/llvm-18/bin/clang")
endif()
if (NOT LLVM_DIR STREQUAL "/usr/lib/llvm-18/cmake" OR
    NOT Clang_DIR STREQUAL "/usr/lib/cmake/clang-18")
    message(FATAL_ERROR "C&1 LLVM/Clang package paths are not reference paths")
endif()

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
if (NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "C&1 verifier requires the Ninja generator; got ${CMAKE_GENERATOR}")
endif()
if (NOT DEFINED CMAKE_MAKE_PROGRAM OR CMAKE_MAKE_PROGRAM STREQUAL "")
    message(FATAL_ERROR "C&1 verifier requires CMAKE_MAKE_PROGRAM to be configured")
endif()
if (NOT EXISTS "${CMAKE_MAKE_PROGRAM}")
    message(FATAL_ERROR "C&1 verifier generator executable does not exist: ${CMAKE_MAKE_PROGRAM}")
endif()
get_filename_component(CAND_MAKE_PROGRAM_REALPATH "${CMAKE_MAKE_PROGRAM}" REALPATH)
if (NOT "${CAND_MAKE_PROGRAM_REALPATH}" STREQUAL "${CAND_SUPPORTED_NINJA_PATH}")
    message(FATAL_ERROR "C&1 verifier generator is not ${CAND_SUPPORTED_NINJA_PATH}: ${CAND_MAKE_PROGRAM_REALPATH}")
endif()
execute_process(
    COMMAND "${CAND_MAKE_PROGRAM_REALPATH}" --version
    RESULT_VARIABLE CAND_BUILD_NINJA_RESULT
    OUTPUT_VARIABLE CAND_BUILD_NINJA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT CAND_BUILD_NINJA_RESULT EQUAL 0)
    message(FATAL_ERROR "unable to execute qualified Ninja ${CAND_MAKE_PROGRAM_REALPATH}")
endif()
if (NOT CAND_BUILD_NINJA STREQUAL CAND_SUPPORTED_NINJA)
    message(FATAL_ERROR "unsupported Ninja ${CAND_BUILD_NINJA} from ${CAND_MAKE_PROGRAM_REALPATH}; expected ${CAND_SUPPORTED_NINJA}")
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
execute_process(
    COMMAND git status --porcelain --untracked-files=all
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CAND_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT CAND_GIT_STATUS STREQUAL "")
    message(FATAL_ERROR "C& verifier source checkout is not clean")
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
    CAND_BUILD_COMPILER_PATH="${CAND_CXX_COMPILER_REALPATH}"
    CAND_TOOLCHAIN_CMAKE_VERSION="${CAND_SUPPORTED_CMAKE}"
    CAND_TOOLCHAIN_NINJA_VERSION="${CAND_SUPPORTED_NINJA}"
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
