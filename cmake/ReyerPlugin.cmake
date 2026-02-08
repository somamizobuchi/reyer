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
    )

  This function:
    - Creates a SHARED library target ``<name>``
    - Sets C++23 as the required standard
    - Links ``reyer::reyer`` (PRIVATE)
    - Links any additional libraries specified via ``LINK_LIBRARIES``
    - Sets the library output directory if ``OUTPUT_DIRECTORY`` is provided
    - Removes the ``lib`` prefix from the output filename

#]=======================================================================]

function(reyer_add_plugin NAME)
    cmake_parse_arguments(
        PARSE_ARGV 1
        ARG
        ""
        "OUTPUT_DIRECTORY"
        "SOURCES;LINK_LIBRARIES"
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
endfunction()
