# Tools CMakeLists.txt for EDS-BIO-FMI (index building and querying only)

set(BIOFMI_TOOLS build locate extract)

foreach(tool ${BIOFMI_TOOLS})
    add_executable(eds-biofmi-${tool} ${tool}.cpp)
    target_link_libraries(eds-biofmi-${tool} biofmi_lib ${Boost_LIBRARIES} ${SDSL_LIBRARY})

    # The tools were called biofmi-<tool> until 2026-11-02, and every xbench spec
    # and run directory under ~/Data/experiments/biofmi names them so. The old
    # name stays as a symlink, next to the binary and where it is installed, so
    # those measurements still reproduce as recorded.
    add_custom_command(TARGET eds-biofmi-${tool} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink eds-biofmi-${tool}
                "$<TARGET_FILE_DIR:eds-biofmi-${tool}>/biofmi-${tool}"
        VERBATIM
    )
    install(TARGETS eds-biofmi-${tool} RUNTIME DESTINATION bin)
    install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink eds-biofmi-${tool}
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/biofmi-${tool}\")")
endforeach()
