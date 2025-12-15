add_executable(bulb main.c)
target_link_libraries(bulb PUBLIC bulb_interface)

configure_file(bulb_version.h.in "${PROJECT_BINARY_DIR}/configured/bulb_version.h")

install(TARGETS bulb DESTINATION bin)