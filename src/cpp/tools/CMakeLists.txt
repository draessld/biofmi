# Tools CMakeLists.txt for BioFMI (index building and querying only)

# Build tool
add_executable(biofmi-build build.cpp)
target_link_libraries(biofmi-build biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Locate tool
add_executable(biofmi-locate locate.cpp)
target_link_libraries(biofmi-locate biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Install targets
install(TARGETS
    biofmi-build
    biofmi-locate
    RUNTIME DESTINATION bin
)
