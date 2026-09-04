cmake_minimum_required(VERSION 3.16)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KisakCODPortableTests LANGUAGES CXX)
endif()

include(CTest)

# Optional native renderer boundary check; portable tests need no DirectX SDK.
set(KISAK_D3DX_TEST_ROOT "" CACHE PATH "Pinned D3DX build/native directory for native JPEG tests")
if (WIN32 AND KISAK_D3DX_TEST_ROOT)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_jpeg_arch x86)
    else()
        set(_jpeg_arch x64)
    endif()
    add_executable(r_savegame_image_win32_tests r_savegame_image_win32_tests.cpp)
    target_compile_features(r_savegame_image_win32_tests PRIVATE cxx_std_20)
    target_compile_definitions(r_savegame_image_win32_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if (MSVC)
        target_compile_options(r_savegame_image_win32_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(r_savegame_image_win32_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(r_savegame_image_win32_tests PRIVATE ../../src "${KISAK_D3DX_TEST_ROOT}/include")
    target_link_libraries(r_savegame_image_win32_tests PRIVATE d3d9 user32
        "${KISAK_D3DX_TEST_ROOT}/release/lib/${_jpeg_arch}/d3dx9.lib")
    add_custom_command(TARGET r_savegame_image_win32_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KISAK_D3DX_TEST_ROOT}/release/bin/${_jpeg_arch}/D3DX9_43.dll"
            "$<TARGET_FILE_DIR:r_savegame_image_win32_tests>")
    add_test(NAME r_savegame_image_win32_tests COMMAND r_savegame_image_win32_tests)
endif()

add_executable(r_image_resample_tests r_image_resample_tests.cpp ../../src/gfx_d3d/r_image_resample.cpp)
add_executable(r_gamma_tests r_gamma_tests.cpp ../../src/gfx_d3d/r_gamma.cpp)
add_executable(r_sampler_tests r_sampler_tests.cpp)
set(_image_quality_tests)
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(r_image_quality_tests r_image_quality_tests.cpp ../../src/gfx_d3d/r_image_quality.cpp)
    target_compile_definitions(r_image_quality_tests PRIVATE KISAK_WEB=1 dNODEBUG=1 NOMINMAX)
    set(_image_quality_tests r_image_quality_tests)
endif()
target_compile_definitions(r_gamma_tests PRIVATE KISAK_WEB=1 dNODEBUG=1 NOMINMAX)
add_executable(ui_savegames_tests ui_savegames_tests.cpp ../../src/ui/ui_savegames.cpp)
target_compile_definitions(ui_savegames_tests PRIVATE KISAK_SP=1 KISAK_WEB=1 dNODEBUG=1 NOMINMAX)
foreach(target r_image_resample_tests r_gamma_tests r_sampler_tests ${_image_quality_tests} ui_savegames_tests)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ../../src ../../deps)
    target_compile_definitions(${target} PRIVATE RELEASE_ASSERTS=1)
    if (MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_options(${target} PRIVATE -fdeclspec)
        target_compile_definitions(${target} PRIVATE __int8=char __int16=short __int32=int "__int64=long long")
        target_link_options(${target} PRIVATE "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1")
        set_target_properties(${target} PROPERTIES SUFFIX ".cjs")
        find_program(KISAK_SAVE_NODE_EXECUTABLE NAMES node nodejs REQUIRED)
        add_test(NAME ${target} COMMAND "${KISAK_SAVE_NODE_EXECUTABLE}" "$<TARGET_FILE:${target}>")
    else()
        add_test(NAME ${target} COMMAND ${target})
    endif()
endforeach()

add_executable(r_text_tests r_text_tests.cpp ../../src/gfx_d3d/r_text.cpp)
target_compile_features(r_text_tests PRIVATE cxx_std_20)
target_include_directories(r_text_tests PRIVATE ../../src)
if (MSVC)
    target_compile_options(r_text_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(r_text_tests PRIVATE -UNDEBUG)
endif()
if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_options(r_text_tests PRIVATE -fdeclspec)
    target_compile_definitions(r_text_tests PRIVATE KISAK_WEB=1 __int16=short __int32=int "__int64=long long" __forceinline=inline)
    target_link_options(r_text_tests PRIVATE "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1")
    set_target_properties(r_text_tests PROPERTIES SUFFIX ".cjs")
    find_program(KISAK_TEXT_NODE_EXECUTABLE NAMES node nodejs REQUIRED)
    add_test(NAME r_text_tests COMMAND "${KISAK_TEXT_NODE_EXECUTABLE}" "$<TARGET_FILE:r_text_tests>")
else()
    add_test(NAME r_text_tests COMMAND r_text_tests)
endif()

option(KISAK_BUILD_FUZZERS "Build the native imported-asset parser fuzz target" OFF)

get_filename_component(KISAK_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# The portable Linux test host compiles selected IW3 headers without the
# Win32 platform preamble that normally provides this calling-convention
# keyword. On non-MSVC native hosts it has no ABI effect.
if (NOT MSVC AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_compile_definitions(__cdecl=)
endif()

add_executable(iwd_archive_tests
    "${CMAKE_CURRENT_LIST_DIR}/iwd_archive_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwd_archive.cpp"
)
if ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Canonical unzip owns IW3 structures with the 32-bit engine ABI. Keep the
    # platform-neutral archive parser available to 64-bit native test hosts.
    target_sources(iwd_archive_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/unzip.cpp")
    target_compile_definitions(iwd_archive_tests PRIVATE
        KISAK_TEST_CANONICAL_MINIZIP=1)
endif()

if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(cgame_timescale_tests
        "${CMAKE_CURRENT_LIST_DIR}/cgame_timescale_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/cgame/cg_servercmds.cpp")
    target_compile_features(cgame_timescale_tests PRIVATE cxx_std_20)
    target_include_directories(cgame_timescale_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(cgame_timescale_tests PRIVATE
        KISAK_SP=1 KISAK_WEB=1 KISAK_OPENAL=1
        KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1 dNODEBUG=1)
    target_compile_options(cgame_timescale_tests PRIVATE
        -ffunction-sections -fdata-sections -UNDEBUG -fdeclspec)
    target_link_options(cgame_timescale_tests PRIVATE -Wl,--gc-sections)
endif()

add_executable(iwi_image_tests
    "${CMAKE_CURRENT_LIST_DIR}/iwi_image_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwi_image.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_image_wavelet.cpp"
)

add_executable(canonical_asset_abi_tests
    "${CMAKE_CURRENT_LIST_DIR}/canonical_asset_abi_tests.cpp"
)

add_executable(qcommon_math_tests
    "${CMAKE_CURRENT_LIST_DIR}/qcommon_math_tests.cpp"
)

add_executable(fx_emission_math_tests
    "${CMAKE_CURRENT_LIST_DIR}/fx_emission_math_tests.cpp"
)

add_executable(fx_archive_callback_tests
    "${CMAKE_CURRENT_LIST_DIR}/fx_archive_callback_tests.cpp"
)

add_executable(sound_curve_validation_tests
    "${CMAKE_CURRENT_LIST_DIR}/sound_curve_validation_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/universal/com_sndalias_curve.cpp"
)

add_executable(canonical_startup_zone_tests
    "${CMAKE_CURRENT_LIST_DIR}/canonical_startup_zone_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/client/cl_fastfile_config.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_asset_load.cpp"
)
target_compile_features(canonical_startup_zone_tests PRIVATE cxx_std_20)
target_include_directories(canonical_startup_zone_tests PRIVATE
    "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
target_compile_definitions(canonical_startup_zone_tests PRIVATE
    KISAK_SP=1 dNODEBUG=1)
if (MSVC)
    target_compile_options(canonical_startup_zone_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(canonical_startup_zone_tests PRIVATE -UNDEBUG)
endif()
if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(canonical_startup_zone_tests PRIVATE KISAK_WEB=1)
endif()

add_executable(web_thread_context_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_thread_context_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_thread_context.cpp"
)

add_executable(web_frame_profile_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_frame_profile_tests.cpp"
)

if ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(gate3_db_pool_tests
        "${CMAKE_CURRENT_LIST_DIR}/gate3_db_pool_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_registry_pools.cpp"
    )
    target_compile_features(gate3_db_pool_tests PRIVATE cxx_std_20)
    target_include_directories(gate3_db_pool_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
    target_compile_definitions(gate3_db_pool_tests PRIVATE
        KISAK_SP=1 KISAK_DB_POOL_STANDALONE=1 dNODEBUG=1)
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_options(gate3_db_pool_tests PRIVATE -fdeclspec)
    endif()

    add_executable(canonical_runtime_prefix_tests
        "${CMAKE_CURRENT_LIST_DIR}/gate3_com_init_trace_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/com_init_trace.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_initialization.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_string_ownership.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/common.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/common_runtime_commands.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/cmd.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_memorytree.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_stringlist.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_constantconfigstrings.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_memory_string.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_shared.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/dvar.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/dvar_cmds.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/q_shared.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/physicalmemory.cpp"
    )
    target_compile_features(canonical_runtime_prefix_tests PRIVATE cxx_std_20)
    target_include_directories(canonical_runtime_prefix_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src")
    target_compile_definitions(canonical_runtime_prefix_tests PRIVATE
        KISAK_SP=1 KISAK_GATE3_COM_INIT_PREFIX=1
        KISAK_RUNTIME_COMMANDS_TEST=1 dNODEBUG=1)
    if (MSVC)
        target_compile_definitions(canonical_runtime_prefix_tests PRIVATE WIN32=1)
        target_compile_options(canonical_runtime_prefix_tests PRIVATE /Gy /Gw /Zc:inline /UNDEBUG)
        target_link_options(canonical_runtime_prefix_tests PRIVATE /OPT:REF /OPT:ICF)
    else()
        target_compile_definitions(canonical_runtime_prefix_tests PRIVATE KISAK_WEB=1)
        target_compile_options(canonical_runtime_prefix_tests PRIVATE -UNDEBUG)
    endif()

    add_executable(canonical_map_request_tests
        "${CMAKE_CURRENT_LIST_DIR}/canonical_map_request_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/client/cl_initialization.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/server/sv_map_assets.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/server/sv_map_command.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/com_init_trace.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/engine_lifecycle_trace.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_initialization.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/common.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/cmd.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_memorytree.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_stringlist.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_constantconfigstrings.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_memory_string.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_shared.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/dvar.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/dvar_cmds.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/q_shared.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/physicalmemory.cpp"
    )
    target_compile_features(canonical_map_request_tests PRIVATE cxx_std_20)
    target_include_directories(canonical_map_request_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(canonical_map_request_tests PRIVATE
        KISAK_SP=1 KISAK_GATE3_COM_INIT_PREFIX=1
        KISAK_CANONICAL_MAP_COMMANDS=1 dNODEBUG=1)
    if (MSVC)
        target_compile_definitions(canonical_map_request_tests PRIVATE WIN32=1)
        target_compile_options(canonical_map_request_tests PRIVATE
            /Gy /Gw /Zc:inline /UNDEBUG)
        target_link_options(canonical_map_request_tests PRIVATE /OPT:REF /OPT:ICF)
    else()
        target_compile_definitions(canonical_map_request_tests PRIVATE KISAK_WEB=1)
        target_compile_options(canonical_map_request_tests PRIVATE -UNDEBUG -fdeclspec)
    endif()

    add_executable(canonical_cm_load_tests
        "${CMAKE_CURRENT_LIST_DIR}/canonical_cm_load_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/cm_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/com_world_runtime.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/engine_lifecycle_trace.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_registry_pools.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/game/g_bsp.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_memory_hunk.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/game/savememory_init.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_variable_init.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_vm_init.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/script/scr_runtime_error.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/xanim/xanim_init.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/xanim/dobj_init.cpp"
    )
    target_compile_features(canonical_cm_load_tests PRIVATE cxx_std_20)
    target_include_directories(canonical_cm_load_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(canonical_cm_load_tests PRIVATE
        KISAK_SP=1 KISAK_CANONICAL_CM_LOAD_TEST=1
        KISAK_DB_SYNC_FILE_TEST=1 dNODEBUG=1)
    if (MSVC)
        target_compile_options(canonical_cm_load_tests PRIVATE
            /Gy /Gw /Zc:inline /UNDEBUG)
        target_link_options(canonical_cm_load_tests PRIVATE /OPT:REF /OPT:ICF)
    else()
        target_compile_definitions(canonical_cm_load_tests PRIVATE KISAK_WEB=1)
        target_compile_options(canonical_cm_load_tests PRIVATE -UNDEBUG -fdeclspec)
    endif()

    add_executable(gate3_db_stream_trace_tests
        "${CMAKE_CURRENT_LIST_DIR}/gate3_db_stream_trace_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_auth.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_file_platform.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_file_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_loaders.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_xmodel.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_xanim.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_xmodel_surface.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_xmodel_physics.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_weapon.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_stringtable.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_comworld.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_gfxworld.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_gameworld.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_mapents.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_clipmap.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_gfxworld_platform.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_image.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_image_platform.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_localize.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_sound_curve.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_sound_alias.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_loaded_sound.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_sound_platform.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_font.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_fx.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_fx_impact.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_light.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_menu.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_menu_expression.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_material.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_material_platform.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_material_technique.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_water.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_memory.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_registry_pools.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_registry_publication.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_assetnames.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/sound_curve_publication_test_stubs.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_sndalias_curve.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_string_ownership.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stringtable_load.cpp"
    )
    target_compile_features(gate3_db_stream_trace_tests PRIVATE cxx_std_20)
    target_include_directories(gate3_db_stream_trace_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(gate3_db_stream_trace_tests PRIVATE
        KISAK_SP=1 KISAK_DB_SYNC_FILE_TEST=1 KISAK_DB_POOL_STANDALONE=1
        KISAK_SOUND_CURVE_PUBLICATION_TEST=1 RELEASE_ASSERTS=1 dNODEBUG=1)
    if (MSVC)
        target_compile_definitions(gate3_db_stream_trace_tests PRIVATE WIN32=1)
        target_compile_options(gate3_db_stream_trace_tests PRIVATE /UNDEBUG)
    else()
        target_compile_definitions(gate3_db_stream_trace_tests PRIVATE KISAK_WEB=1)
        target_compile_options(gate3_db_stream_trace_tests PRIVATE -UNDEBUG -fdeclspec)
    endif()
endif()

if (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Compile the real generated native RawFile routine behind a test-only
    # slice so the browser loader has an independent upstream oracle.
    add_executable(native_db_load_rawfile_oracle_tests
        "${CMAKE_CURRENT_LIST_DIR}/db_load_rawfile_oracle_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_semantic_trace.cpp"
    )
    target_compile_features(native_db_load_rawfile_oracle_tests PRIVATE cxx_std_20)
    target_include_directories(native_db_load_rawfile_oracle_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(native_db_load_rawfile_oracle_tests PRIVATE
        KISAK_SP=1 WIN32=1 KISAK_NATIVE_DB_LOAD_RAWFILE_ORACLE=1
        RELEASE_ASSERTS=1 dNODEBUG=1)
    target_compile_options(native_db_load_rawfile_oracle_tests PRIVATE
        /Gy /Gw /Zc:inline /UNDEBUG)
    target_link_options(native_db_load_rawfile_oracle_tests PRIVATE /OPT:REF /OPT:ICF)
    add_test(NAME native_db_load_rawfile_oracle_tests
        COMMAND native_db_load_rawfile_oracle_tests)
endif()

if ((WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR
    CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(generated_db_load_rawfile_oracle_tests
        "${CMAKE_CURRENT_LIST_DIR}/db_load_rawfile_oracle_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_generated_loaders.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_stream_load.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/database/db_semantic_trace.cpp"
    )
    target_compile_features(generated_db_load_rawfile_oracle_tests PRIVATE cxx_std_20)
    target_include_directories(generated_db_load_rawfile_oracle_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(generated_db_load_rawfile_oracle_tests PRIVATE
        KISAK_SP=1 KISAK_DB_SYNC_FILE_TEST=1
        KISAK_GENERATED_DB_LOAD_RAWFILE_ORACLE=1 RELEASE_ASSERTS=1 dNODEBUG=1)
    if (MSVC)
        target_compile_definitions(generated_db_load_rawfile_oracle_tests PRIVATE WIN32=1)
        target_compile_options(generated_db_load_rawfile_oracle_tests PRIVATE
            /Gy /Gw /Zc:inline /UNDEBUG)
        target_link_options(generated_db_load_rawfile_oracle_tests PRIVATE /OPT:REF /OPT:ICF)
    else()
        target_compile_definitions(generated_db_load_rawfile_oracle_tests PRIVATE KISAK_WEB=1)
        target_compile_options(generated_db_load_rawfile_oracle_tests PRIVATE -UNDEBUG -fdeclspec)
        target_link_options(generated_db_load_rawfile_oracle_tests PRIVATE
            "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1")
        set_target_properties(generated_db_load_rawfile_oracle_tests
            PROPERTIES SUFFIX ".cjs")
    endif()
endif()

add_executable(web_renderer_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_surface.cpp"
)

add_executable(web_renderer_world_scene_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_world_scene_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_world_scene.cpp"
)

if ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR
    CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # The native water implementation consumes canonical Material and world
    # structures whose layout is fixed to the 32-bit IW3 ABI.
    add_executable(r_water_simulation_tests
        "${CMAKE_CURRENT_LIST_DIR}/r_water_simulation_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_water.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/fft.cpp"
    )
endif()

add_executable(web_renderer_lighting_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_lighting_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_lighting.cpp"
)

add_executable(web_renderer_static_model_scene_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_static_model_scene_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_static_model_scene.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_lighting.cpp"
)

add_executable(web_renderer_fx_model_scene_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_fx_model_scene_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_fx_model_scene.cpp"
)

add_executable(web_renderer_primary_light_core_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_primary_light_core_tests.cpp"
)

add_executable(web_renderer_particle_cloud_scene_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_particle_cloud_scene_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_particle_cloud_scene.cpp"
)

add_executable(web_renderer_code_mesh_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_code_mesh_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_code_mesh.cpp"
)

add_executable(web_renderer_mark_mesh_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_mark_mesh_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_mark_mesh.cpp"
)

if ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR
    CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(web_renderer_mark_fragments_tests
        "${CMAKE_CURRENT_LIST_DIR}/web_renderer_mark_fragments_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_mark_fragments.cpp"
    )
endif()

if ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR EMSCRIPTEN OR
    CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # This seam deliberately consumes canonical XModel fields. Its layout
    # assertions require the 32-bit IW3 ABI used by Win32 and Wasm32.
    add_executable(web_renderer_dobj_submission_tests
        "${CMAKE_CURRENT_LIST_DIR}/web_renderer_dobj_submission_tests.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/cgame/cg_pose_utils.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_light_attachment.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_dobj_lod.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_dobj_scene.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_lighting.cpp"
    )
endif()

add_executable(web_openal_proxy_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_openal_proxy_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_openal_proxy.cpp"
)

add_executable(web_browser_bindings_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_browser_bindings_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_browser_bindings.cpp"
)

add_executable(web_asset_probe_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_asset_probe_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_asset_probe.cpp"
)

add_library(kisak_test_semantic_trace STATIC
    "${KISAK_REPOSITORY_ROOT}/src/database/db_semantic_trace.cpp")
target_compile_features(kisak_test_semantic_trace PRIVATE cxx_std_20)
target_include_directories(kisak_test_semantic_trace PUBLIC
    "${KISAK_REPOSITORY_ROOT}/src")

target_link_libraries(canonical_asset_abi_tests PRIVATE kisak_test_semantic_trace)
if (TARGET gate3_db_stream_trace_tests)
    target_link_libraries(gate3_db_stream_trace_tests PRIVATE kisak_test_semantic_trace)
endif()

target_compile_features(iwd_archive_tests PRIVATE cxx_std_20)
target_include_directories(iwd_archive_tests PRIVATE
    "${KISAK_REPOSITORY_ROOT}/src"
    "${KISAK_REPOSITORY_ROOT}/deps/zlib"
    "${KISAK_REPOSITORY_ROOT}/deps")
target_compile_features(iwi_image_tests PRIVATE cxx_std_20)
target_include_directories(iwi_image_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_definitions(iwi_image_tests PRIVATE KISAK_PORTABLE_IWI_WAVELET=1)
target_compile_features(canonical_asset_abi_tests PRIVATE cxx_std_20)
target_include_directories(canonical_asset_abi_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(qcommon_math_tests PRIVATE cxx_std_20)
target_include_directories(qcommon_math_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(fx_emission_math_tests PRIVATE cxx_std_20)
target_include_directories(fx_emission_math_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(fx_emission_math_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(fx_emission_math_tests PRIVATE -UNDEBUG)
endif()
if (TARGET web_renderer_mark_fragments_tests)
    target_compile_features(web_renderer_mark_fragments_tests PRIVATE cxx_std_20)
    target_include_directories(web_renderer_mark_fragments_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src"
        "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(web_renderer_mark_fragments_tests PRIVATE
        KISAK_SP=1 KISAK_WEB=1)
    if (MSVC)
        target_compile_options(web_renderer_mark_fragments_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(web_renderer_mark_fragments_tests PRIVATE -UNDEBUG)
    endif()
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_options(web_renderer_mark_fragments_tests PRIVATE -fdeclspec)
    endif()
endif()
target_compile_features(fx_archive_callback_tests PRIVATE cxx_std_20)
target_include_directories(fx_archive_callback_tests PRIVATE
    "${KISAK_REPOSITORY_ROOT}/src"
    "${KISAK_REPOSITORY_ROOT}/deps")
target_compile_definitions(fx_archive_callback_tests PRIVATE
    KISAK_WEB=1 KISAK_SP=1)
if (MSVC)
    target_compile_options(fx_archive_callback_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(fx_archive_callback_tests PRIVATE -UNDEBUG)
endif()
if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_options(fx_archive_callback_tests PRIVATE -fdeclspec)
endif()
target_compile_features(sound_curve_validation_tests PRIVATE cxx_std_20)
target_include_directories(sound_curve_validation_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(sound_curve_validation_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(sound_curve_validation_tests PRIVATE -UNDEBUG)
endif()
target_compile_features(web_thread_context_tests PRIVATE cxx_std_20)
target_include_directories(web_thread_context_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_definitions(web_thread_context_tests PRIVATE KISAK_SP=1)
target_compile_features(web_frame_profile_tests PRIVATE cxx_std_20)
target_include_directories(web_frame_profile_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(web_frame_profile_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_frame_profile_tests PRIVATE -UNDEBUG)
endif()
target_compile_features(web_renderer_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_world_scene_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_world_scene_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (TARGET r_water_simulation_tests)
    target_compile_features(r_water_simulation_tests PRIVATE cxx_std_20)
    target_include_directories(r_water_simulation_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src")
    target_compile_definitions(r_water_simulation_tests PRIVATE
        KISAK_WEB=1 dNODEBUG=1)
    if (MSVC)
        target_compile_options(r_water_simulation_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(r_water_simulation_tests PRIVATE -UNDEBUG -fdeclspec)
    endif()
endif()
target_compile_features(web_renderer_lighting_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_lighting_tests PRIVATE
    "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(web_renderer_lighting_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_renderer_lighting_tests PRIVATE -UNDEBUG)
endif()
target_compile_features(web_renderer_static_model_scene_tests PRIVATE cxx_std_20)
# The canonical runtime headers require IW3's 32-bit ABI. Keep the existing
# portable scene tests available on 64-bit hosts without weakening ABI assertions.
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_sources(web_renderer_static_model_scene_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_dpvs_core.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/cm_showcollision.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_world_scene.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_math.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_math_anglevectors.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/com_convexhull.cpp"
    )
    target_include_directories(web_renderer_static_model_scene_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(web_renderer_static_model_scene_tests PRIVATE
        KISAK_TEST_DPVS_CORE=1 KISAK_SP=1 KISAK_GATE3_COM_INIT_PREFIX=1 dNODEBUG=1)
    if (EMSCRIPTEN)
        target_compile_definitions(web_renderer_static_model_scene_tests PRIVATE KISAK_WEB=1)
        target_compile_options(web_renderer_static_model_scene_tests PRIVATE -fdeclspec)
    endif()
    if (MSVC)
        target_compile_options(web_renderer_static_model_scene_tests PRIVATE /Gy /Gw /Zc:inline /UNDEBUG)
        target_link_options(web_renderer_static_model_scene_tests PRIVATE /OPT:REF /OPT:ICF)
    else()
        target_compile_options(web_renderer_static_model_scene_tests PRIVATE
            -ffunction-sections -fdata-sections -UNDEBUG)
        target_link_options(web_renderer_static_model_scene_tests PRIVATE -Wl,--gc-sections)
    endif()
endif()

target_include_directories(web_renderer_static_model_scene_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_fx_model_scene_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_fx_model_scene_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_primary_light_core_tests PRIVATE cxx_std_20)
target_compile_definitions(web_renderer_primary_light_core_tests PRIVATE KISAK_WEB=1)
target_include_directories(web_renderer_primary_light_core_tests PRIVATE
    "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(web_renderer_primary_light_core_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_renderer_primary_light_core_tests PRIVATE -UNDEBUG)
endif()
if (MSVC)
    target_compile_options(web_renderer_fx_model_scene_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_renderer_fx_model_scene_tests PRIVATE -UNDEBUG)
endif()
target_compile_features(web_renderer_particle_cloud_scene_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_particle_cloud_scene_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(web_renderer_particle_cloud_scene_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_renderer_particle_cloud_scene_tests PRIVATE -UNDEBUG)
endif()
target_compile_features(web_renderer_code_mesh_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_code_mesh_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_mark_mesh_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_mark_mesh_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
if (MSVC)
    target_compile_options(web_renderer_mark_mesh_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_renderer_mark_mesh_tests PRIVATE -UNDEBUG)
endif()
if (TARGET web_renderer_dobj_submission_tests)
    target_compile_features(web_renderer_dobj_submission_tests PRIVATE cxx_std_20)
    target_include_directories(web_renderer_dobj_submission_tests PRIVATE
        "${KISAK_REPOSITORY_ROOT}/src" "${KISAK_REPOSITORY_ROOT}/deps")
    target_compile_definitions(web_renderer_dobj_submission_tests PRIVATE
        KISAK_SP=1 dNODEBUG=1)
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_definitions(web_renderer_dobj_submission_tests PRIVATE
            KISAK_WEB=1
            __int64=long\ long __int32=int __int16=short __int8=char)
        target_compile_options(web_renderer_dobj_submission_tests PRIVATE
            -fdeclspec)
    endif()
    if (MSVC)
        target_compile_options(web_renderer_dobj_submission_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(web_renderer_dobj_submission_tests PRIVATE -UNDEBUG)
    endif()
endif()
target_compile_features(web_openal_proxy_tests PRIVATE cxx_std_20)
target_include_directories(web_openal_proxy_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_browser_bindings_tests PRIVATE cxx_std_20)
target_include_directories(web_browser_bindings_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_asset_probe_tests PRIVATE cxx_std_20)
target_include_directories(web_asset_probe_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")

if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Use Emscripten's maintained zlib port, matching the browser target.
    target_compile_definitions(iwd_archive_tests PRIVATE KISAK_WEB=1)
    target_compile_options(iwd_archive_tests PRIVATE "-sUSE_ZLIB=1")
    foreach(target
        canonical_asset_abi_tests
        qcommon_math_tests
        fx_emission_math_tests
        fx_archive_callback_tests
        canonical_startup_zone_tests
        web_thread_context_tests
        gate3_db_pool_tests
        web_renderer_surface_tests
        web_renderer_world_scene_tests
        web_renderer_lighting_tests
        web_renderer_static_model_scene_tests
        web_renderer_primary_light_core_tests
        web_renderer_particle_cloud_scene_tests
        web_renderer_mark_mesh_tests
        web_renderer_mark_fragments_tests
        web_renderer_dobj_submission_tests
        web_openal_proxy_tests
        web_browser_bindings_tests
        web_asset_probe_tests
    )
        if (TARGET ${target})
            target_link_options(${target} PRIVATE
                "-sENVIRONMENT=node"
                "-sEXIT_RUNTIME=1"
                "-sNO_DISABLE_EXCEPTION_CATCHING")
        endif()
    endforeach()
    target_link_options(iwd_archive_tests PRIVATE
        "-sUSE_ZLIB=1"
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(iwi_image_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "-sINITIAL_MEMORY=33554432"
    )
    if (TARGET canonical_runtime_prefix_tests)
        target_link_options(canonical_runtime_prefix_tests PRIVATE
            "-sENVIRONMENT=node"
            "-sEXIT_RUNTIME=1"
            "-sNO_DISABLE_EXCEPTION_CATCHING"
            "-sERROR_ON_UNDEFINED_SYMBOLS=1"
            "-sALLOW_MEMORY_GROWTH=1"
        )
    endif()
    if (TARGET canonical_map_request_tests)
        target_link_options(canonical_map_request_tests PRIVATE
            "-sENVIRONMENT=node"
            "-sEXIT_RUNTIME=1"
            "-sNO_DISABLE_EXCEPTION_CATCHING"
            "-sERROR_ON_UNDEFINED_SYMBOLS=1"
            "-sALLOW_MEMORY_GROWTH=1"
        )
    endif()
    if (TARGET canonical_cm_load_tests)
        target_link_options(canonical_cm_load_tests PRIVATE
            "-sENVIRONMENT=node"
            "-sEXIT_RUNTIME=1"
            "-sNO_DISABLE_EXCEPTION_CATCHING"
            "-sERROR_ON_UNDEFINED_SYMBOLS=1"
        )
    endif()
    target_link_options(web_renderer_code_mesh_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "-sALLOW_MEMORY_GROWTH=1"
    )
    # The unchanged 24-cloud rollback fixture exceeds the default 16 MiB heap.
    target_compile_options(web_renderer_particle_cloud_scene_tests PRIVATE
        "-sNO_DISABLE_EXCEPTION_CATCHING")
    target_link_options(web_renderer_particle_cloud_scene_tests PRIVATE
        "-sALLOW_MEMORY_GROWTH=1")
    foreach(target
        iwd_archive_tests
        iwi_image_tests
        canonical_asset_abi_tests
        qcommon_math_tests
        cgame_timescale_tests
        fx_emission_math_tests
        fx_archive_callback_tests
        sound_curve_validation_tests
        canonical_startup_zone_tests
        web_thread_context_tests
        web_frame_profile_tests
        canonical_runtime_prefix_tests
        canonical_map_request_tests
        canonical_cm_load_tests
        gate3_db_pool_tests
        web_renderer_surface_tests
        web_renderer_world_scene_tests
        r_water_simulation_tests
        web_renderer_lighting_tests
        web_renderer_static_model_scene_tests
        web_renderer_fx_model_scene_tests
        web_renderer_primary_light_core_tests
        web_renderer_particle_cloud_scene_tests
        web_renderer_code_mesh_tests
        web_renderer_mark_mesh_tests
        web_renderer_mark_fragments_tests
        web_renderer_dobj_submission_tests
        web_openal_proxy_tests
        web_browser_bindings_tests
        web_asset_probe_tests
        gate3_db_stream_trace_tests
    )
        if (TARGET ${target})
            set_target_properties(${target} PROPERTIES SUFFIX ".cjs")
        endif()
    endforeach()
    find_program(KISAK_NODE_EXECUTABLE NAMES node node.exe REQUIRED)
    if (TARGET gate3_db_stream_trace_tests)
        target_compile_options(gate3_db_stream_trace_tests PRIVATE "-sUSE_ZLIB=1")
        target_link_options(gate3_db_stream_trace_tests PRIVATE
            "-sUSE_ZLIB=1" "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1")
    endif()
else()
    if (WIN32)
        enable_language(C)
        add_library(kisak_test_zlib STATIC
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/adler32.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/compress.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/crc32.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/deflate.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/gzio.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/infblock.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/infcodes.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/inffast.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/inflate.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/inftrees.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/infutil.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/trees.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/uncompr.c"
            "${KISAK_REPOSITORY_ROOT}/deps/zlib/zutil.c"
        )
        target_include_directories(kisak_test_zlib PUBLIC
            "${KISAK_REPOSITORY_ROOT}/deps/zlib")
        set(KISAK_TEST_ZLIB_TARGET kisak_test_zlib)
    else()
        find_package(ZLIB REQUIRED)
        set(KISAK_TEST_ZLIB_TARGET ZLIB::ZLIB)
    endif()
    target_link_libraries(iwd_archive_tests PRIVATE ${KISAK_TEST_ZLIB_TARGET})
    if (TARGET gate3_db_stream_trace_tests)
        target_link_libraries(gate3_db_stream_trace_tests PRIVATE ${KISAK_TEST_ZLIB_TARGET})
    endif()
endif()

foreach(target
    iwd_archive_tests
    iwi_image_tests
    canonical_asset_abi_tests
    qcommon_math_tests
    fx_emission_math_tests
    fx_archive_callback_tests
    sound_curve_validation_tests
    canonical_startup_zone_tests
    web_thread_context_tests
    web_frame_profile_tests
    cgame_timescale_tests
    canonical_runtime_prefix_tests
    canonical_map_request_tests
    canonical_cm_load_tests
    gate3_db_pool_tests
    gate3_db_stream_trace_tests
    generated_db_load_rawfile_oracle_tests
    web_renderer_surface_tests
    web_renderer_world_scene_tests
    r_water_simulation_tests
    web_renderer_lighting_tests
    web_renderer_static_model_scene_tests
    web_renderer_fx_model_scene_tests
    web_renderer_primary_light_core_tests
    web_renderer_particle_cloud_scene_tests
    web_renderer_code_mesh_tests
    web_renderer_mark_mesh_tests
    web_renderer_mark_fragments_tests
    web_renderer_dobj_submission_tests
    web_openal_proxy_tests
    web_browser_bindings_tests
    web_asset_probe_tests
)
    if (TARGET ${target})
        if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
            add_test(NAME ${target}
                COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:${target}>")
        else()
            add_test(NAME ${target} COMMAND ${target})
        endif()
    endif()
endforeach()

if (TARGET gate3_db_stream_trace_tests)
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        add_test(NAME canonical_xfile_mutation_tests
            COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:gate3_db_stream_trace_tests>" --mutate)
    else()
        add_test(NAME canonical_xfile_mutation_tests
            COMMAND gate3_db_stream_trace_tests --mutate)
    endif()
    set_tests_properties(canonical_xfile_mutation_tests PROPERTIES TIMEOUT 60)

    # Reuse the same generated-loader fixtures with the real zone coordinator,
    # PMem allocator and single-Worker database scheduler instead of stand-ins.
    get_target_property(db_test_sources gate3_db_stream_trace_tests SOURCES)
    add_executable(canonical_db_zone_recovery_tests ${db_test_sources}
        "${KISAK_REPOSITORY_ROOT}/src/database/db_registry.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/universal/physicalmemory.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_thread_context.cpp")
    foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES SUFFIX)
        get_target_property(db_test_property gate3_db_stream_trace_tests ${property})
        if (NOT db_test_property STREQUAL "db_test_property-NOTFOUND")
            set_property(TARGET canonical_db_zone_recovery_tests PROPERTY ${property} "${db_test_property}")
        endif()
    endforeach()
    target_compile_definitions(canonical_db_zone_recovery_tests PRIVATE
        KISAK_DB_ZONE_RECOVERY_TEST=1 KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1
        KISAK_GATE3_COM_INIT_PREFIX=1 KISAK_WEB=1)
    target_compile_features(canonical_db_zone_recovery_tests PRIVATE cxx_std_20)
    if (MSVC)
        target_compile_options(canonical_db_zone_recovery_tests PRIVATE /Gy /Gw)
        target_link_options(canonical_db_zone_recovery_tests PRIVATE /OPT:REF)
    endif()
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        add_test(NAME canonical_db_zone_recovery_tests
            COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:canonical_db_zone_recovery_tests>")
    else()
        add_test(NAME canonical_db_zone_recovery_tests COMMAND canonical_db_zone_recovery_tests)
    endif()
    set_tests_properties(canonical_db_zone_recovery_tests PROPERTIES TIMEOUT 60)
endif()

if (KISAK_BUILD_FUZZERS)
    if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "KISAK_BUILD_FUZZERS requires a native Clang toolchain")
    endif()
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "KISAK_BUILD_FUZZERS requires Clang/libFuzzer")
    endif()

    add_executable(asset_parsers_fuzz
        "${CMAKE_CURRENT_LIST_DIR}/../fuzz/asset_parsers_fuzz.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwd_archive.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwi_image.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/gfx_d3d/r_image_wavelet.cpp"
    )
    target_compile_features(asset_parsers_fuzz PRIVATE cxx_std_20)
    target_compile_definitions(asset_parsers_fuzz PRIVATE
        KISAK_PORTABLE_IWI_WAVELET=1)
    target_include_directories(asset_parsers_fuzz PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
    target_compile_options(asset_parsers_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(asset_parsers_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
    target_link_libraries(asset_parsers_fuzz PRIVATE ${KISAK_TEST_ZLIB_TARGET})

endif()
