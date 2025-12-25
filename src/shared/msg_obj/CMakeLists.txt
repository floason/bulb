add_library(bulb_msg_obj INTERFACE)
target_include_directories(bulb_msg_obj INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bulb_msg_obj INTERFACE bulb_interface)

# Translation units to propagate must be explicitly defined using target_sources
# to prevent linking errors!
target_sources(bulb_msg_obj INTERFACE bulb_obj.c stdout_obj.c userinfo_obj.c connect_obj.c 
    disconnect_obj.c)