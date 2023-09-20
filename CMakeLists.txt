cmake_minimum_required(VERSION 3.20)

project(chip8 C)

set(CMAKE_C_STANDARD 11)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(src)

add_subdirectory(src)
