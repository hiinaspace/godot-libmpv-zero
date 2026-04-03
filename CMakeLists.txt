# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.17)

# Silence unused variable warning when specified from toolchain
if(CMAKE_C_COMPILER)
endif()

set(LIBNAME "libmpv_zero")
set(GODOT_PROJECT_DIR "project" CACHE STRING "The directory of a Godot project folder")
set(MPV_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies/mpv-dev" CACHE PATH "Path to a staged libmpv development package")

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

# Ensure godot-cpp submodule has been updated
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/godot-cpp/src")
    message(NOTICE "godot-cpp bindings source not found")
    message(NOTICE "initializing/updating the godot-cpp submodule...")

    # update the c++ bindings submodule to populate it with the necessary source for the library
    execute_process(
        COMMAND git submodule update --init godot-cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
add_subdirectory(godot-cpp SYSTEM)

# Add godot-cpp's module path and include the exported functions.
# This is made available for documentation generation
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${godot-cpp_SOURCE_DIR}/cmake")
include(GodotCPPModule)

# The godot-cpp target has some of useful properties attached that can be retrieved like so.
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(godot-libmpv-zero
    VERSION 1.0
    DESCRIPTION "Godot 4 GDExtension experiments for libmpv-backed zero-copy video playback."
    HOMEPAGE_URL "https://github.com/hiinaspace/godot-libmpv-zero"
    LANGUAGES CXX
)

add_library(${LIBNAME} SHARED)

target_sources(${LIBNAME}
    PRIVATE
    src/register_types.cpp
    src/register_types.h
    src/channel_audio_stream.cpp
    src/channel_audio_stream.h
    src/mpv_core.cpp
    src/mpv_core.h
    src/runtime_library_utils.cpp
    src/runtime_library_utils.h
    src/mini_mpv_client.h
    src/audio_bridge.cpp
    src/audio_bridge.h
    src/video_output_backend.h
    src/render_thread_service.cpp
    src/render_thread_service.h
    src/vk_video_output.cpp
    src/vk_video_output.h
    src/mpv_player.cpp
    src/mpv_player.h
    src/mpv_texture.cpp
    src/mpv_texture.h
    src/mini_vulkan.h
)

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if(DOC_XML)
    if(GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} ${DOC_XML})
    endif()
endif()

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# Require at least C++17 for this target
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

set_target_properties(${LIBNAME}
    PROPERTIES
    # The generator expression here prevents msvc from adding a Debug or Release subdir.
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

    PREFIX ""
    OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)

if(WIN32)
    if(NOT EXISTS "${MPV_DIR}/bin/libmpv-2.dll")
        message(STATUS "No staged libmpv runtime found at ${MPV_DIR}/bin/libmpv-2.dll; skipping runtime staging")
    endif()
    file(GLOB MPV_RUNTIME_DLLS "${MPV_DIR}/bin/*.dll")
    if(MPV_RUNTIME_DLLS)
        foreach(MPV_RUNTIME_DLL IN LISTS MPV_RUNTIME_DLLS)
            add_custom_command(TARGET ${LIBNAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPV_RUNTIME_DLL}" "${GODOT_PROJECT_BINARY_DIR}/"
                COMMENT "Staging libmpv runtime from ${MPV_RUNTIME_DLL}"
            )
        endforeach()
    else()
        message(STATUS "No libmpv runtime DLLs found at ${MPV_DIR}/bin; skipping runtime staging")
    endif()
endif()

