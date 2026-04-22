add_executable(bulb main.c cli_client.c cli_server.c cli_util.c)
target_include_directories(bulb PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb PUBLIC bulb_interface bulb_util bulb_client bulb_server)

install(TARGETS bulb DESTINATION bin)