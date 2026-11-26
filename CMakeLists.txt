# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
cmake_minimum_required(VERSION 3.15)
project(MeowSynth CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

option(MEOWSYNTH_BUILD_TESTS "Build the native tests (engine vs reference renders)" ON)

# The DSP reproduces the original's floating-point arithmetic exactly: IEEE double/float
# operations in program order (no fused multiply-add, no x87 extended precision) and the
# rounding-mode switches some loops make.
set(MEOW_FP_FLAGS -frounding-math -ffp-contract=off)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND MEOW_FP_FLAGS -msse2 -mfpmath=sse)
endif()

file(GLOB DSP_SOURCES src/dsp/*.cpp src/engine/*.cpp)
add_library(MeowCore STATIC ${DSP_SOURCES})
target_include_directories(MeowCore PUBLIC src)
target_compile_options(MeowCore PRIVATE ${MEOW_FP_FLAGS})

if(WIN32)
    file(GLOB_RECURSE DAMSDK_SOURCES src/damsdk/api/*.cpp src/damsdk/gui/*.cpp)
    file(GLOB PLUGIN_SOURCES src/plugin/*.cpp)
    # The interface needs the bitmaps from your copy of the original (tools/extract_assets.py);
    # without them the plugin builds without an editor (hosts show their generic controls).
    if(EXISTS ${CMAKE_SOURCE_DIR}/assets/interface/background.bmp)
        message(STATUS "MeowSynth: building the interface (assets/interface found)")
        file(GLOB GUI_SOURCES src/gui/*.cpp)
        list(APPEND PLUGIN_SOURCES ${GUI_SOURCES} resources.rc)
        set(MEOW_HAS_GUI ON)
    else()
        message(STATUS "MeowSynth: no assets/interface, building without the interface")
    endif()
    add_library(MeowSynth SHARED ${PLUGIN_SOURCES} ${DAMSDK_SOURCES} exports.def)
    if(MEOW_HAS_GUI)
        target_compile_definitions(MeowSynth PRIVATE MEOW_HAS_GUI)
    endif()
    target_include_directories(MeowSynth PRIVATE src)
    if(MINGW AND CMAKE_CROSSCOMPILING)
        target_include_directories(MeowSynth PRIVATE cmake/mingw-include)  # <Windows.h> on a case-sensitive host
    endif()
    target_link_libraries(MeowSynth PRIVATE MeowCore kernel32 user32 gdi32 ole32 shell32 uuid)
    set_target_properties(MeowSynth PROPERTIES PREFIX "" OUTPUT_NAME "MeowSynth" SUFFIX ".dll")
    if(MINGW)
        # Link the C++ runtime in, so hosts don't need MinGW's DLLs.
        target_link_options(MeowSynth PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(MEOWSYNTH_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(render_scenarios tests/engine/render_scenarios.cpp)
    target_include_directories(render_scenarios PRIVATE tests/helpers)
    target_link_libraries(render_scenarios PRIVATE MeowCore)
    target_compile_options(render_scenarios PRIVATE ${MEOW_FP_FLAGS})
    add_test(NAME engine_matches_original COMMAND render_scenarios ${CMAKE_SOURCE_DIR}/tests/fixtures all)
    add_test(NAME engine_matches_original_host37 COMMAND render_scenarios ${CMAKE_SOURCE_DIR}/tests/fixtures/host37 all 37)
    # 1000-sample host blocks: SynthEdit's block length becomes 100 (FUN_1005d140)
    add_test(NAME engine_matches_original_host1000 COMMAND render_scenarios ${CMAKE_SOURCE_DIR}/tests/fixtures/host1000 all 1000)
endif()
