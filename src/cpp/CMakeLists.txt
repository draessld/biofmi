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
    NAMES sdsl
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

# Enable testing
enable_testing()

# Add subdirectories
add_subdirectory(lib)
add_subdirectory(tools)

# Add test executables
set(TEST_DIR ${CMAKE_SOURCE_DIR}/../../tests/cpp)

# Test: EDS parsing
add_executable(test_eds ${TEST_DIR}/test_eds.cpp)
target_link_libraries(test_eds biofmi_lib)
add_test(NAME test_eds COMMAND test_eds)

# Test: Sources parsing
add_executable(test_sources ${TEST_DIR}/test_sources.cpp)
target_link_libraries(test_sources biofmi_lib)
add_test(NAME test_sources COMMAND test_sources)

# Test: Statistics
add_executable(test_stats ${TEST_DIR}/test_stats.cpp)
target_link_libraries(test_stats biofmi_lib)
add_test(NAME test_stats COMMAND test_stats)

# Test: Merging
add_executable(test_merge ${TEST_DIR}/test_merge.cpp)
target_link_libraries(test_merge biofmi_lib)
add_test(NAME test_merge COMMAND test_merge)

# Test: Transformation (EDS to l-EDS)
add_executable(test_transform ${TEST_DIR}/test_transform.cpp)
target_link_libraries(test_transform biofmi_lib)
add_test(NAME test_transform COMMAND test_transform)

# Test: MSA transformation
add_executable(test_msa ${TEST_DIR}/test_msa.cpp)
target_link_libraries(test_msa biofmi_lib)
add_test(NAME test_msa COMMAND test_msa)

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
