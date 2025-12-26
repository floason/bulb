cmake_minimum_required(VERSION 3.19)
project(bulb LANGUAGES C VERSION 0.0.1)

add_library(bulb_interface INTERFACE)
target_compile_features(bulb_interface INTERFACE c_std_11)
target_compile_definitions(bulb_interface INTERFACE _CRT_SECURE_NO_WARNINGS)    # Ignore MSVC security warnings.
target_include_directories(bulb_interface INTERFACE 
    "${PROJECT_BINARY_DIR}/configured"
    "${PROJECT_SOURCE_DIR}/src"
)

if(WIN32)
    target_link_libraries(bulb_interface INTERFACE Ws2_32)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory(src)