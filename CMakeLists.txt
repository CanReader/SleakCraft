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

# Doxygen documentation target
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen..."
        VERBATIM
    )
endif()

