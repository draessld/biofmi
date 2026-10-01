# Tools CMakeLists.txt for BioFMI (index building and querying only)

# Build tool
add_executable(biofmi-build build.cpp)
target_link_libraries(biofmi-build biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Locate tool
add_executable(biofmi-locate locate.cpp)
target_link_libraries(biofmi-locate biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Extract tool: a stretch of one genome, from the index plus its sources
add_executable(biofmi-extract extract.cpp)
target_link_libraries(biofmi-extract biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Install targets
install(TARGETS
    biofmi-build
    biofmi-locate
    biofmi-extract
    RUNTIME DESTINATION bin
)
