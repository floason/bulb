cmake_minimum_required(VERSION 3.19)
project(bulb LANGUAGES C VERSION 0.2.2)

add_library(bulb_interface INTERFACE)
target_compile_features(bulb_interface INTERFACE c_std_11)
target_include_directories(bulb_interface INTERFACE 
    "${PROJECT_BINARY_DIR}/configured"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
)

# Compiles BULB_EXPORT for any supplied target and additionally
# BULB_SHARED_LIBRARY if it is a shared library. This also sets
# C_VISIBILITY_PRESET to hidden, preventing every function from
# getting exported by default.
function(bulb_compile_export_macros TARGET_NAME)
    target_compile_definitions(${TARGET_NAME} PRIVATE BULB_EXPORT)

    get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
    if(TARGET_TYPE STREQUAL "SHARED_LIBRARY")
        target_compile_definitions(${TARGET_NAME} PRIVATE BULB_SHARED_LIBRARY)
    endif()
    
    set_target_properties(${TARGET_NAME} PROPERTIES C_VISIBILITY_PRESET hidden)
endfunction()

# Ignore MSVC security warnings.
target_compile_definitions(bulb_interface INTERFACE _CRT_SECURE_NO_WARNINGS)    

# Link Ws2_32.lib to bulb_interface on Windows for Winsock2.
if(WIN32)
    target_link_libraries(bulb_interface INTERFACE Ws2_32)
endif()

set(BULB_BUILD_DIR_NAME "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Base>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/${BULB_BUILD_DIR_NAME}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/${BULB_BUILD_DIR_NAME}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/${BULB_BUILD_DIR_NAME}")

add_subdirectory(src)