cmake_minimum_required(VERSION 3.12)
project(bulb LANGUAGES C VERSION 0.0.0)

add_library(bulb_interface INTERFACE)
target_compile_features(bulb_interface INTERFACE c_std_11)
target_compile_definitions(bulb_interface INTERFACE _CRT_SECURE_NO_WARNINGS)
target_include_directories(bulb_interface INTERFACE 
    "${PROJECT_BINARY_DIR}/configured"
    "src/shared"
)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory(src)