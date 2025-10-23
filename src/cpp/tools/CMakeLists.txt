# Tools CMakeLists.txt

# Transform tool
add_executable(biofmi-transform transform.cpp)
target_link_libraries(biofmi-transform biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Build tool
add_executable(biofmi-build build.cpp)
target_link_libraries(biofmi-build biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Locate tool
add_executable(biofmi-locate locate.cpp)
target_link_libraries(biofmi-locate biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Stats tool
add_executable(biofmi-stats stats.cpp)
target_link_libraries(biofmi-stats biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Generate patterns tool
add_executable(biofmi-genpatterns genpatterns.cpp)
target_link_libraries(biofmi-genpatterns biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

# Install targets
install(TARGETS
    biofmi-transform
    biofmi-build
    biofmi-locate
    biofmi-stats
    biofmi-genpatterns
    RUNTIME DESTINATION bin
)
