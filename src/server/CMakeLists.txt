add_executable(bulb_server main.c)
target_compile_definitions(bulb_server PUBLIC SERVER)
target_include_directories(bulb_server PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_server PUBLIC bulb_interface bulb_shared)

install(TARGETS bulb_server DESTINATION bin)