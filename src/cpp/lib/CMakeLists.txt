# Library CMakeLists.txt for BioFMI (now depends on EDSParser)

# Create library from source files (only index-related code)
set(LIB_SOURCES
    index/index.cpp
)

set(LIB_HEADERS
    index/index.hpp
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

# Install targets
install(TARGETS biofmi_lib
    ARCHIVE DESTINATION lib
)

install(FILES ${LIB_HEADERS}
    DESTINATION include/biofmi
)
