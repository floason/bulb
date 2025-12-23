add_library(bulb_shared INTERFACE)
target_include_directories(bulb_shared INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_shared INTERFACE bulb_interface)

# Translation units to propagate must be explicitly defined using target_sources
# to prevent linking errors!
target_sources(bulb_shared INTERFACE server_node.c)