add_library(bulb_client ${BULB_LIB_TYPE} client.c)
target_compile_definitions(bulb_client PRIVATE CLIENT)
target_include_directories(bulb_client PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_client PRIVATE bulb_interface bulb_shared)

bulb_compile_export_macros(bulb_client)

install(TARGETS bulb_client DESTINATION lib)