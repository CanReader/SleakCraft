cmake_minimum_required(VERSION 3.31)
project(SleakEngine)

include(cmake/SleakCommon.cmake)

if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_DEBUG DEBUG)
endif()

# Actual projects
add_subdirectory(Engine)
add_subdirectory(Game)
add_subdirectory(Client)
