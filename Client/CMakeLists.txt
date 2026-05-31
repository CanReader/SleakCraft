cmake_minimum_required(VERSION 3.31)

project(Client)

file(GLOB_RECURSE CLIENT_SOURCES "src/*.cpp")

add_executable(SleakCraft ${CLIENT_SOURCES})

target_link_libraries(SleakCraft PRIVATE SleakGame Engine)

target_include_directories(SleakCraft PRIVATE ${CMAKE_SOURCE_DIR}/Game/include)

# --- Asset copy: per-file dependency tracking ---
# POST_BUILD only fires when the exe re-links, so shader edits (that change no
# .cpp) used to leave bin/assets stale. Track each asset as its own custom
# command so edits propagate to bin/ on every `cmake --build`.
#
# Game/assets is called first so Game copies of shared filenames win (same
# precedence as the previous copy_directory ordering). Engine-side
# sleak_track_asset_dir skips any output already registered by Game.
set(_asset_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets")

set(_sleak_seen_outputs "")

function(sleak_track_asset_dir SRC_DIR LABEL)
    file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${SRC_DIR}/*")
    set(_outputs "")
    foreach(_src ${_files})
        file(RELATIVE_PATH _rel "${SRC_DIR}" "${_src}")
        set(_dst "${_asset_out_dir}/${_rel}")
        list(FIND _sleak_seen_outputs "${_dst}" _seen_idx)
        if(NOT _seen_idx EQUAL -1)
            continue()
        endif()
        add_custom_command(
            OUTPUT "${_dst}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
            DEPENDS "${_src}"
            COMMENT "Copy ${LABEL} asset: ${_rel}"
            VERBATIM
        )
        list(APPEND _outputs "${_dst}")
        list(APPEND _sleak_seen_outputs "${_dst}")
    endforeach()
    set(SLEAK_ASSET_OUTPUTS ${SLEAK_ASSET_OUTPUTS} ${_outputs} PARENT_SCOPE)
    set(_sleak_seen_outputs ${_sleak_seen_outputs} PARENT_SCOPE)
endfunction()

set(SLEAK_ASSET_OUTPUTS "")
sleak_track_asset_dir("${CMAKE_SOURCE_DIR}/Game/assets"          "game")
sleak_track_asset_dir("${CMAKE_SOURCE_DIR}/Engine/Engine/assets" "engine")

add_custom_target(CopyAssets ALL DEPENDS ${SLEAK_ASSET_OUTPUTS})
add_dependencies(SleakCraft CopyAssets)

# --- Post-build: copy runtime DLLs (Windows) ---
if(WIN32)
    add_custom_command(TARGET SleakCraft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:SleakCraft>
            $<TARGET_FILE_DIR:SleakCraft>
        COMMAND_EXPAND_LISTS
    )
endif()
