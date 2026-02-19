#[=======================================================================[.rst:
ReyerPlugin
-----------

Helper function for building reyer plugins.

.. command:: reyer_add_plugin

  Create a shared library plugin target for reyer::

    reyer_add_plugin(<name>
        SOURCES <source1> [source2 ...]
        [LINK_LIBRARIES <lib1> [lib2 ...]]
        [OUTPUT_DIRECTORY <dir>]
        [INSTALL]
        [RESOURCES <file_or_dir1> [file_or_dir2 ...]]
    )

  This function:
    - Creates a SHARED library target ``<name>``
    - Sets C++23 as the required standard
    - Links ``reyer::reyer`` (PRIVATE)
    - Links any additional libraries specified via ``LINK_LIBRARIES``
    - Sets the library output directory if ``OUTPUT_DIRECTORY`` is provided
    - Removes the ``lib`` prefix from the output filename
    - If ``INSTALL`` is specified, generates install() rules that place
      the plugin and any ``RESOURCES`` into
      ``$ENV{HOME}/.local/share/reyer/plugins/<name>/``.
      Each resource entry may be a file or a directory; directories are
      installed recursively, preserving their name.

#]=======================================================================]

function(reyer_add_plugin NAME)
    cmake_parse_arguments(
        PARSE_ARGV 1
        ARG
        "INSTALL"
        "OUTPUT_DIRECTORY"
        "SOURCES;LINK_LIBRARIES;RESOURCES"
    )

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "reyer_add_plugin(${NAME}): SOURCES must be specified")
    endif()

    add_library(${NAME} SHARED)

    target_sources(${NAME} PRIVATE ${ARG_SOURCES})

    target_compile_features(${NAME} PRIVATE cxx_std_23)

    target_link_libraries(${NAME} PRIVATE reyer::reyer)

    if(ARG_LINK_LIBRARIES)
        target_link_libraries(${NAME} PRIVATE ${ARG_LINK_LIBRARIES})
    endif()

    set_target_properties(${NAME} PROPERTIES PREFIX "")

    if(ARG_OUTPUT_DIRECTORY)
        set_target_properties(${NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIRECTORY}"
        )
    endif()

    if(ARG_INSTALL)
        set(_plugin_dest "$ENV{HOME}/.local/share/reyer/plugins/${NAME}")

        install(
            TARGETS ${NAME}
            LIBRARY DESTINATION "${_plugin_dest}"
            RUNTIME DESTINATION "${_plugin_dest}"
        )

        foreach(_res IN LISTS ARG_RESOURCES)
            if(IS_DIRECTORY "${_res}")
                install(
                    DIRECTORY "${_res}"
                    DESTINATION "${_plugin_dest}"
                )
            else()
                install(
                    FILES "${_res}"
                    DESTINATION "${_plugin_dest}"
                )
            endif()
        endforeach()
    endif()
endfunction()
