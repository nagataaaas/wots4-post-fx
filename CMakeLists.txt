cmake_minimum_required(VERSION 3.25)
project(wots4_mod_post_fx LANGUAGES C CXX RC)

# When building standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>.
if(NOT COMMAND wots4_add_mod)
    set(WOTS4_SDK_DIR "" CACHE PATH "Path to wots4-mods (ModLoader)")
    if(NOT EXISTS "${WOTS4_SDK_DIR}/cmake/Wots4.cmake")
        message(FATAL_ERROR "Set WOTS4_SDK_DIR to the path of wots4-mods")
    endif()
    include(${WOTS4_SDK_DIR}/cmake/Wots4.cmake)
    enable_testing()
    set(WOTS4_BUILD_TESTS ON)
endif()

wots4_use_minhook()
find_program(POST_FX_COMPILER fxc
    HINTS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    REQUIRED)
set(shader_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${shader_directory}")
foreach(shader IN ITEMS copy fxaa motion_blur smaa_edges smaa_weights smaa_blend sharpen)
    add_custom_command(OUTPUT "${shader_directory}/${shader}.cso"
        COMMAND "${POST_FX_COMPILER}" /nologo /T ps_3_0 /E main /O3
            /Fo "${shader_directory}/${shader}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.hlsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/smaa_common.hlsli"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl" VERBATIM)
    list(APPEND shader_outputs "${shader_directory}/${shader}.cso")
endforeach()
add_custom_target(post_fx_shaders DEPENDS ${shader_outputs})
configure_file(src/shaders.rc.in "${CMAKE_CURRENT_BINARY_DIR}/shaders.rc" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/shaders.rc" PROPERTIES OBJECT_DEPENDS "${shader_outputs}")

wots4_add_mod(post_fx NO_RELEASE
    SOURCES src/mod.cpp src/settings.cpp src/game_render.cpp src/post_process.cpp src/device_state.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/shaders.rc"
    DEF src/mod.def LIBS wots4_minhook d3d9
    FILES config.ini README.md README.en.md
    LICENSES docs/licenses/minhook.txt docs/licenses/smaa.txt docs/licenses/cas.txt)
target_include_directories(mod_post_fx PRIVATE src)
target_compile_definitions(mod_post_fx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(mod_post_fx post_fx_shaders)

if(WOTS4_BUILD_TESTS)
    wots4_add_probe(post_fx_settings_probe SOURCES tests/settings_probe.cpp src/settings.cpp INCLUDES src
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
    wots4_add_probe(post_fx_motion_probe SOURCES tests/motion_probe.cpp INCLUDES src)
    wots4_add_probe(post_fx_device_probe SOURCES tests/device_probe.cpp src/device_state.cpp src/post_process.cpp src/settings.cpp
        INCLUDES src LIBS d3d9 ARGS $<TARGET_FILE:mod_post_fx>)
    target_compile_definitions(post_fx_device_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_dependencies(post_fx_device_probe mod_post_fx)
    wots4_add_probe(post_fx_shader_probe SOURCES tests/shader_probe.cpp src/device_state.cpp src/post_process.cpp src/settings.cpp
        INCLUDES src LIBS d3d9 ARGS $<TARGET_FILE:mod_post_fx>)
    target_compile_definitions(post_fx_shader_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_dependencies(post_fx_shader_probe mod_post_fx)
    set_tests_properties(post_fx_shader_probe PROPERTIES LABELS gpu SKIP_RETURN_CODE 77)
    set(WOTS4_UNPACKED_EXE "${WOTS4_ROOT}/research/bin/WayOfTheSamurai4.unpacked.exe" CACHE FILEPATH "Decrypted exe")
    wots4_add_probe(post_fx_native_probe SOURCES tests/native_probe.cpp INCLUDES src
        LINK_OPTIONS /BASE:0x10000000 /DYNAMICBASE
        ARGS "${WOTS4_UNPACKED_EXE}" $<TARGET_FILE:mod_post_fx> NEEDS_EXE)
    add_dependencies(post_fx_native_probe mod_post_fx)
    foreach(scenario IN ITEMS unsupported conflict registration steam ssaa layout scene_conflict)
        add_test(NAME post_fx_native_${scenario} COMMAND post_fx_native_probe
            "${WOTS4_UNPACKED_EXE}" $<TARGET_FILE:mod_post_fx> ${scenario})
        set_tests_properties(post_fx_native_${scenario} PROPERTIES LABELS needs_exe)
    endforeach()
endif()
