# Library CMakeLists.txt for BioFMI (now depends on EDSParser)

# Build-time git stamp of this repository, for `--version` (COMMIT, COMMIT_DATE,
# DIRTY). The script is edsparser's, run against BioFMI's tree; it is re-run on
# every build so a binary reports the commit it was really built from. The
# header is named apart from edsparser's own build_info.hpp, which lands in the
# same generated directory.
get_filename_component(BIOFMI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(BIOFMI_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(BIOFMI_BUILD_INFO_HPP "${BIOFMI_GENERATED_DIR}/biofmi_build_info.hpp")
set(BIOFMI_STAMP_ARGS
    -D "GIT_SRC_DIR=${BIOFMI_REPO_ROOT}"
    -D "IN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/build_info.hpp.in"
    -D "OUT_FILE=${BIOFMI_BUILD_INFO_HPP}"
    -D "BIOFMI_VERSION=${PROJECT_VERSION}"
    -P "${BIOFMI_REPO_ROOT}/external/edsparser/cmake/GitVersion.cmake"
)
add_custom_target(biofmi_build_info ALL
    COMMAND ${CMAKE_COMMAND} ${BIOFMI_STAMP_ARGS}
    BYPRODUCTS "${BIOFMI_BUILD_INFO_HPP}"
    COMMENT "Stamping BioFMI build with git commit"
    VERBATIM
)
# Once at configure time too, so the first compile has a header to include.
execute_process(COMMAND ${CMAKE_COMMAND} ${BIOFMI_STAMP_ARGS})

# Create library from source files (only index-related code)
set(LIB_SOURCES
    index/index.cpp
    version.cpp
)

set(LIB_HEADERS
    index/index.hpp
    version.hpp
)

# Create static library
add_library(biofmi_lib STATIC ${LIB_SOURCES})

# Link libraries
target_link_libraries(biofmi_lib
    edsparser_lib
    ${Boost_LIBRARIES}
    ${SDSL_LIBRARY}
    ${DIVSUFSORT_LIBRARY}
    ${DIVSUFSORT64_LIBRARY}
)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(biofmi_lib OpenMP::OpenMP_CXX)
endif()

# Set include directories for the library
target_include_directories(biofmi_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(biofmi_lib PRIVATE ${BIOFMI_GENERATED_DIR})
add_dependencies(biofmi_lib biofmi_build_info)

# Install targets
install(TARGETS biofmi_lib
    ARCHIVE DESTINATION lib
)

install(FILES ${LIB_HEADERS}
    DESTINATION include/biofmi
)
