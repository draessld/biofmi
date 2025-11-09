# Library CMakeLists.txt

# Create library from source files
set(LIB_SOURCES
    eds.cpp
    index.cpp
    common.cpp
    transform_utils.cpp
    msa_utils.cpp
)

set(LIB_HEADERS
    eds.hpp
    index.hpp
    utils.hpp
    common.hpp
)

# Create static library
add_library(biofmi_lib STATIC ${LIB_SOURCES})

# Link libraries
target_link_libraries(biofmi_lib
    ${Boost_LIBRARIES}
    ${SDSL_LIBRARY}
)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(biofmi_lib OpenMP::OpenMP_CXX)
endif()

# Set include directories for the library
target_include_directories(biofmi_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install targets
install(TARGETS biofmi_lib
    ARCHIVE DESTINATION lib
)

install(FILES ${LIB_HEADERS}
    DESTINATION include/biofmi
)
