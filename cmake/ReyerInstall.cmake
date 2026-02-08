include(CMakePackageConfigHelpers)

set(REYER_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/reyer"
    CACHE PATH "CMake package config install location"
)
mark_as_advanced(REYER_INSTALL_CMAKEDIR)

install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT reyer_Development
)

install(
    TARGETS reyer
    EXPORT reyerTargets
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    EXPORT reyerTargets
    NAMESPACE reyer::
    DESTINATION "${REYER_INSTALL_CMAKEDIR}"
    COMPONENT reyer_Development
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/reyerConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/reyerConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/reyerConfig.cmake"
    INSTALL_DESTINATION "${REYER_INSTALL_CMAKEDIR}"
)

install(
    FILES
        "${PROJECT_BINARY_DIR}/reyerConfig.cmake"
        "${PROJECT_BINARY_DIR}/reyerConfigVersion.cmake"
    DESTINATION "${REYER_INSTALL_CMAKEDIR}"
    COMPONENT reyer_Development
)

install(
    FILES "${PROJECT_SOURCE_DIR}/cmake/ReyerPlugin.cmake"
    DESTINATION "${REYER_INSTALL_CMAKEDIR}"
    COMPONENT reyer_Development
)
