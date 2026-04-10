configure_file(bulb_version.h.in "${PROJECT_BINARY_DIR}/configured/bulb_version.h")

add_library(bulb_util INTERFACE)

# Translation units to propagate must be explicitly defined using target_sources
# to prevent linking errors!
target_sources(bulb_util PUBLIC trie.c util.c console_io.c)

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)