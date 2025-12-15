configure_file(bulb_version.h.in "${PROJECT_BINARY_DIR}/configured/bulb_version.h")

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)