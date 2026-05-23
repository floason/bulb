configure_file(bulb_version.h.in "${PROJECT_BINARY_DIR}/configured/bulb_version.h")

add_library(bulb_util INTERFACE)

# Translation units to propagate must be explicitly defined using target_sources
# to prevent linking errors!
target_sources(bulb_util PUBLIC trie.c util.c console_io.c)

# Building the CLI which links both the server and client Bulb library targets
# requires that they are both compiled as shared libraries. As both libraries
# inherit functons from its respective shared target with the exact same name,
# using static libraries will result in a One-Definition-Rule violation, resulting
# in undefined behaviour.
set(BULB_LIB_TYPE)
if(BULB_BUILD_CLI)
    set(BULB_LIB_TYPE SHARED)
endif()

# Create the server/client protocol libraries, which both interface with the shared
# interface library.
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)

# If desired, create a binary that supplements a basic command-line interface for
# using the Bulb protocol, which can be launched using either the client or server
# library.
if(BULB_BUILD_CLI)
    add_subdirectory(cli)
endif()