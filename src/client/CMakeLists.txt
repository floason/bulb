add_executable(bulb_client main.c client.c)
target_compile_definitions(bulb_client PUBLIC CLIENT)
target_include_directories(bulb_client PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_client PUBLIC bulb_interface bulb_shared)

install(TARGETS bulb_client DESTINATION bin)