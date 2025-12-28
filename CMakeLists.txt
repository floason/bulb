cmake_minimum_required(VERSION 3.19)
project(bulb LANGUAGES C VERSION 0.0.2)

add_library(bulb_interface INTERFACE)
target_compile_features(bulb_interface INTERFACE c_std_11)
target_include_directories(bulb_interface INTERFACE 
    "${PROJECT_BINARY_DIR}/configured"
    "${PROJECT_SOURCE_DIR}/src"
)

# Ignore MSVC security warnings.
target_compile_definitions(bulb_interface INTERFACE _CRT_SECURE_NO_WARNINGS)    

# Link Ws2_32.lib to bulb_interface on Windows.
if(WIN32)
    target_link_libraries(bulb_interface INTERFACE Ws2_32)

# While WinAPI provides sufficient functions for per-character stdin input and
# cursor positioning, this requires me to interface with terminal commands
# directly on Linux and I figured that relying on ncurses would be a much more
# viable alternative.
else()
    set(CURSES_NEED_NCURSES TRUE)
    find_package(Curses)
    if(NOT CURSES_FOUND)
        message(FATAL_ERROR "ncurses not found!")
    endif()

    add_library(Curses::Curses INTERFACE IMPORTED)
    set_target_properties(Curses::Curses PROPERTIES
        INTERFACE_LINK_LIBRARIES "${CURSES_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIRS}"
    )
    target_link_libraries(bulb_interface INTERFACE Curses::Curses)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory(src)