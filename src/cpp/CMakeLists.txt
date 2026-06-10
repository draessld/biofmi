# CMakeLists.txt for BIO-FMI
cmake_minimum_required(VERSION 3.10)
project(BioFMI VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(BEFORE ${CMAKE_INCLUDE_PATH})

# Link directories
link_directories(BEFORE ${CMAKE_LIBRARY_PATH})

# Find required packages
find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

# Add EDSParser library from submodule (lib/ only — avoids pulling in edsparser tools and tests)
set(EDSPARSER_LIB_DIR "${CMAKE_SOURCE_DIR}/../../external/edsparser/src/cpp/lib")
add_subdirectory("${EDSPARSER_LIB_DIR}" "${CMAKE_BINARY_DIR}/edsparser_lib")
message(STATUS "Using EDSParser from submodule: ${EDSPARSER_LIB_DIR}")

# Create a build-time include alias so that #include <edsparser/...> resolves
# to the submodule's lib/ directory (mirrors the installed layout under include/edsparser/)
set(_edsparser_iface_dir "${CMAKE_BINARY_DIR}/edsparser_include")
file(MAKE_DIRECTORY "${_edsparser_iface_dir}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${EDSPARSER_LIB_DIR}"
        "${_edsparser_iface_dir}/edsparser"
    ERROR_QUIET
)
include_directories(BEFORE "${_edsparser_iface_dir}")

# Find OpenMP for parallel processing
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "Found OpenMP: ${OpenMP_CXX_VERSION}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    message(WARNING "OpenMP not found. Parallel processing will be disabled.")
endif()

# Check for SDSL
find_path(SDSL_INCLUDE_DIR sdsl/suffix_arrays.hpp
    PATHS
        ${CMAKE_INCLUDE_PATH}
        $ENV{HOME}/include
        /usr/local/include
        /usr/include
)

if(SDSL_INCLUDE_DIR)
    message(STATUS "Found SDSL: ${SDSL_INCLUDE_DIR}")
    include_directories(${SDSL_INCLUDE_DIR})
else()
    message(WARNING "SDSL not found. Please install from https://github.com/simongog/sdsl-lite")
endif()

find_library(SDSL_LIBRARY
    NAMES sdsl sdsl-lite
    PATHS
        ${CMAKE_LIBRARY_PATH}
        $ENV{HOME}/lib
        /usr/local/lib
        /usr/lib
)

if(SDSL_LIBRARY)
    message(STATUS "Found SDSL library: ${SDSL_LIBRARY}")
else()
    message(WARNING "SDSL library not found")
endif()

# Find divsufsort libraries (required by SDSL for SA construction)
find_library(DIVSUFSORT_LIBRARY
    NAMES divsufsort
    PATHS
        ${CMAKE_LIBRARY_PATH}
        $ENV{HOME}/lib
        /usr/local/lib
        /usr/lib
)

find_library(DIVSUFSORT64_LIBRARY
    NAMES divsufsort64
    PATHS
        ${CMAKE_LIBRARY_PATH}
        $ENV{HOME}/lib
        /usr/local/lib
        /usr/lib
)

if(DIVSUFSORT_LIBRARY)
    message(STATUS "Found divsufsort library: ${DIVSUFSORT_LIBRARY}")
else()
    message(WARNING "divsufsort library not found")
endif()

if(DIVSUFSORT64_LIBRARY)
    message(STATUS "Found divsufsort64 library: ${DIVSUFSORT64_LIBRARY}")
else()
    message(WARNING "divsufsort64 library not found")
endif()

# Enable testing
enable_testing()

# Add subdirectories
add_subdirectory(lib)
add_subdirectory(tools)

# Add test executables (only index-related tests)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/../../tests/unit)

# Test: Index building
add_executable(test_build ${TEST_DIR}/test_build.cpp)
target_link_libraries(test_build biofmi_lib)
add_test(NAME test_build COMMAND test_build)

# Test: parse_eds() structural correctness (internal arrays + bit vectors)
add_executable(test_build_structure ${TEST_DIR}/test_build_structure.cpp)
target_link_libraries(test_build_structure biofmi_lib)
add_test(NAME test_build_structure COMMAND test_build_structure)

# Test: Pattern locate
add_executable(test_locate ${TEST_DIR}/test_locate.cpp)
target_link_libraries(test_locate biofmi_lib)
add_test(NAME test_locate COMMAND test_locate)

# Test: Comprehensive locate validation
add_executable(test_locate_validation ${TEST_DIR}/test_locate_validation_simple.cpp)
target_link_libraries(test_locate_validation biofmi_lib)
add_test(NAME test_locate_validation COMMAND test_locate_validation)

# Test: Spec-driven locate correctness (brute-force oracle vs index)
add_executable(test_locate_correctness ${TEST_DIR}/test_locate_correctness.cpp)
target_link_libraries(test_locate_correctness biofmi_lib)
add_test(NAME test_locate_correctness COMMAND test_locate_correctness)

# Test: Offset arithmetic cross-check (snapshot byte positions vs locate results)
add_executable(test_locate_offset ${TEST_DIR}/test_locate_offset.cpp)
target_link_libraries(test_locate_offset biofmi_lib)
add_test(NAME test_locate_offset COMMAND test_locate_offset)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  CMake version       : ${CMAKE_VERSION}")
message(STATUS "  Compiler            : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type          : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Install prefix      : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Boost version       : ${Boost_VERSION}")
message(STATUS "")
