cmake_minimum_required(VERSION 3.31)

project(SleakGame)

file(GLOB_RECURSE GAME_SOURCES "src/*.cpp")

add_library(SleakGame SHARED ${GAME_SOURCES})

target_compile_definitions(SleakGame PRIVATE SLEAK_EXPORTS)

target_compile_definitions(SleakGame PRIVATE SLEAKCRAFT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")

target_include_directories(SleakGame PUBLIC
                          ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(SleakGame PRIVATE Engine)

# --- SPIR-V shaders (committed .spv are only the fallback without glslc) ---
set(GAME_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(GAME_SHADER_INCLUDE_DIR "${GAME_SHADER_DIR}/include")
file(GLOB GAME_VULKAN_SHADERS CONFIGURE_DEPENDS
    "${GAME_SHADER_DIR}/*.vert"
    "${GAME_SHADER_DIR}/*.frag"
)
list(FILTER GAME_VULKAN_SHADERS EXCLUDE REGEX "_gl\\.(vert|frag)$")
file(GLOB_RECURSE GAME_SHADER_INCLUDES CONFIGURE_DEPENDS
    "${GAME_SHADER_INCLUDE_DIR}/*.glsl")
# Engine include edits must rebuild game SPIR-V too
get_property(_engine_include_dir GLOBAL PROPERTY ENGINE_SHADER_INCLUDE_DIR)
get_property(_engine_includes GLOBAL PROPERTY ENGINE_SHADER_INCLUDES)
if(NOT _engine_include_dir)
    set(_engine_include_dir "${CMAKE_SOURCE_DIR}/Engine/assets/shaders/include")
endif()

find_program(GLSLC glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "${VULKAN_SDK}/Bin")
set(GAME_SPIRV_FILES "")
if(GLSLC)
    set(_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_spirv_dir}")
    foreach(_src ${GAME_VULKAN_SHADERS})
        get_filename_component(_name "${_src}" NAME)
        set(_spv "${_spirv_dir}/${_name}.spv")
        add_custom_command(
            OUTPUT "${_spv}"
            COMMAND ${GLSLC} -I "${_engine_include_dir}"
                    -I "${GAME_SHADER_INCLUDE_DIR}" "${_src}" -o "${_spv}"
            DEPENDS "${_src}" ${_engine_includes} ${GAME_SHADER_INCLUDES}
            COMMENT "Compiling ${_name} to SPIR-V"
            VERBATIM
        )
        list(APPEND GAME_SPIRV_FILES "${_spv}")
    endforeach()
else()
    foreach(_src ${GAME_VULKAN_SHADERS})
        if(NOT EXISTS "${_src}.spv")
            message(FATAL_ERROR
                "glslc not found and ${_src} has no committed SPIR-V. "
                "Install the Vulkan SDK or set VULKAN_SDK.")
        endif()
    endforeach()
endif()
add_custom_target(GameShaders ALL DEPENDS ${GAME_SPIRV_FILES})
# Client stages these over the committed copies
set_property(GLOBAL PROPERTY GAME_SPIRV_FILES ${GAME_SPIRV_FILES})
