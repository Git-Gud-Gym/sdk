cmake_minimum_required(VERSION 3.20)
# Git Gud Gym SDK: the shared trainer core, the GameAdapter interface, and the contract between
# the launcher and game modules. Consumed with FetchContent by the launcher and every module repo.
project(GitGudGymSDK VERSION 0.1.0 LANGUAGES CXX RC)

set(GGG_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "Git Gud Gym SDK root")

# The launcher <-> module contract: shared-memory layout, file locations, version resources.
add_library(ggg_contract INTERFACE)
add_library(ggg::contract ALIAS ggg_contract)
target_include_directories(ggg_contract INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ggg_contract INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

set(GGG_SDK_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/game_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/trainer.cpp
    CACHE INTERNAL "Trainer core compiled into every module")

# Writes <binary dir>/generated/<name>/version.h (GGG_V_STRING, GGG_V_MAJOR, ...) from "x.y.z".
function(ggg_write_version_header name version)
    string(REPLACE "." ";" parts "${version}")
    list(GET parts 0 V_MAJOR)
    list(GET parts 1 V_MINOR)
    list(GET parts 2 V_PATCH)
    set(V_STRING "${version}")
    configure_file(${GGG_SDK_DIR}/include/version.h.in ${CMAKE_BINARY_DIR}/generated/${name}/version.h)
endfunction()

# Builds a game module: gitgudgym-<ID>.dll from your adapter sources plus the trainer core, with its
# version read from module.json. Output (dll + module.json + ini) goes to OUTPUT_DIR, which you can
# point the launcher at with the GGG_DEV_MODULES environment variable.
#
#   ggg_add_module(ID eldenring MANIFEST module.json INI gitgudgym-eldenring.ini RC src/module.rc
#                  SOURCES src/dllmain.cpp src/eldenring_game.cpp ...)
function(ggg_add_module)
    cmake_parse_arguments(M "" "ID;MANIFEST;INI;RC;OUTPUT_DIR" "SOURCES" ${ARGN})
    if(NOT M_OUTPUT_DIR)
        set(M_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin/${M_ID})
    endif()
    get_filename_component(manifest_path ${M_MANIFEST} ABSOLUTE)
    get_filename_component(ini_path ${M_INI} ABSOLUTE)
    file(READ ${manifest_path} manifest)
    string(JSON version GET "${manifest}" version)
    string(JSON manifest_id GET "${manifest}" id)
    if(NOT manifest_id STREQUAL M_ID)
        message(FATAL_ERROR "module.json id '${manifest_id}' doesn't match ggg_add_module(ID ${M_ID})")
    endif()
    ggg_write_version_header(${M_ID} ${version})

    set(target module_${M_ID})
    add_library(${target} SHARED ${M_SOURCES} ${GGG_SDK_CORE_SOURCES} ${M_RC})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME gitgudgym-${M_ID}
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${M_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${M_OUTPUT_DIR})
    target_link_libraries(${target} PRIVATE ggg::contract)
    target_include_directories(${target} PRIVATE ${GGG_SDK_DIR}/core ${CMAKE_BINARY_DIR}/generated/${M_ID})
    target_compile_options(${target} PRIVATE /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${manifest_path} ${ini_path} ${M_OUTPUT_DIR})
endfunction()

# Building the SDK on its own (CI) compiles the core against a stub adapter to catch breakage.
if(PROJECT_IS_TOP_LEVEL)
    ggg_add_module(ID sdkcheck MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/check/module.json
                   INI ${CMAKE_CURRENT_SOURCE_DIR}/check/gitgudgym-sdkcheck.ini
                   RC ${CMAKE_CURRENT_SOURCE_DIR}/check/module.rc
                   SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/check/check_module.cpp)
endif()
