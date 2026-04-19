add_library(bulb_server ${BULB_LIB_TYPE} server.c)
target_compile_definitions(bulb_server PRIVATE SERVER)
target_include_directories(bulb_server PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_server PRIVATE bulb_interface bulb_shared)

bulb_compile_export_macros(bulb_server)

install(TARGETS bulb_server DESTINATION lib)