message("----- KisakCOD Browser Platform Slice -----")

project(KisakCOD-web LANGUAGES C CXX)

option(KISAK_WEB_DIAGNOSTICS "Build the separate browser diagnostic artifact" OFF)

include(${PLATFORM_DIR}/web/platform.cmake)
include(${SCRIPTS_DIR}/common_files.cmake)
include(${SCRIPTS_DIR}/sp/sp_files.cmake)
list(REMOVE_DUPLICATES CGAME)

# Loose-source XAnim parsing is a native development path. Browser startup is
# fastfile-only; keep the canonical runtime XAnim owners while excluding the
# renderer-coupled loose-file importer.
list(REMOVE_ITEM XANIM "${SRC_DIR}/xanim/xanim_load_obj.cpp")
list(REMOVE_ITEM XANIM
    "${SRC_DIR}/xanim/xmodel_load_obj.cpp"
    "${SRC_DIR}/xanim/xmodel_load_phys_collmap.cpp")

# The production target already owns these platform/bootstrap translation
# units explicitly above. The remaining inventories are the canonical SP
# client/cgame runtime reached after the DB-owned map publication.
list(REMOVE_ITEM CLIENT
    "${SRC_DIR}/client/cl_cin.cpp"
    "${SRC_DIR}/client/cl_fastfile_config.cpp"
    "${SRC_DIR}/client/cl_initialization.cpp"
    "${SRC_DIR}/client/cl_main_pc.cpp"
    "${SRC_DIR}/client/cl_controller.cpp")
list(REMOVE_ITEM UI "${SRC_DIR}/ui/mission.cpp")

list(REMOVE_ITEM PHYSICS_ODE "${SRC_DIR}/physics/ode/odemath.cpp")
# Loose editor FX parsing is a native development path. Runtime registration
# is owned by the canonical fastfile-backed frontend implementation.
list(REMOVE_ITEM EFFECTSCORE "${SRC_DIR}/EffectsCore/fx_load_obj.cpp")

# Browser audio uses the canonical sound mixer and the OpenAL driver. Miles,
# native dynamic-driver loading, and loose-object development loading are not
# linkable browser integrations.
list(REMOVE_ITEM SOUND
    "${SRC_DIR}/sound/snd_mss.cpp")

set_source_files_properties(
    "${SRC_DIR}/sound/snd_driver_load_obj.cpp"
    PROPERTIES COMPILE_DEFINITIONS "DR_WAV_IMPLEMENTATION;DR_MP3_IMPLEMENTATION")

# These canonical script owners are already compiled into the shared runtime
# prefix used by both production and the strict Node boundary check.
list(REMOVE_ITEM SCRIPT
    "${SRC_DIR}/script/scr_memorytree.cpp"
    "${SRC_DIR}/script/scr_stringlist.cpp")

set(KISAK_WEB_SITE_DIR "${CMAKE_BINARY_DIR}/site")
set(KISAK_WEB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/web")

# List the canonical qcommon/runtime prefix once. It is intentionally compiled
# into two object variants because the production runtime admits the map/DB
# boundary while the strict Node check must stop before that boundary.
set(KISAK_WEB_RUNTIME_PREFIX_SOURCES
    "${SRC_DIR}/database/db_initialization.cpp"
    "${SRC_DIR}/qcommon/com_init_trace.cpp"
    "${SRC_DIR}/qcommon/common.cpp"
    "${SRC_DIR}/qcommon/cmd.cpp"
    "${SRC_DIR}/script/scr_memorytree.cpp"
    "${SRC_DIR}/script/scr_stringlist.cpp"
    "${SRC_DIR}/universal/com_constantconfigstrings.cpp"
    "${SRC_DIR}/universal/com_shared.cpp"
    "${SRC_DIR}/universal/dvar.cpp"
    "${SRC_DIR}/universal/dvar_cmds.cpp"
    "${SRC_DIR}/universal/q_shared.cpp"
    "${SRC_DIR}/universal/physicalmemory.cpp"
)
add_library(kisak_web_runtime_prefix OBJECT ${KISAK_WEB_RUNTIME_PREFIX_SOURCES})
target_include_directories(kisak_web_runtime_prefix PRIVATE "${SRC_DIR}" "${DEPS_DIR}")
kisak_configure_web_compile_target(kisak_web_runtime_prefix)
target_compile_definitions(kisak_web_runtime_prefix PRIVATE
    KISAK_GATE3_COM_INIT_PREFIX=1
    KISAK_CANONICAL_MAP_COMMANDS=1
    KISAK_WEB_FULL_RUNTIME_COMMANDS=1
    KISAK_RUNTIME_MAP_DB_BOUNDARY=1)

# Keep the strict prefix oracle genuinely narrow. Production compiles the same
# canonical sources with the admitted runtime boundary above, while this copy
# must continue to link and run without filesystem, DB, save, or game owners.
add_library(kisak_web_runtime_prefix_check OBJECT
    ${KISAK_WEB_RUNTIME_PREFIX_SOURCES})
target_include_directories(kisak_web_runtime_prefix_check PRIVATE
    "${SRC_DIR}" "${DEPS_DIR}")
kisak_configure_web_compile_target(kisak_web_runtime_prefix_check)
target_compile_definitions(kisak_web_runtime_prefix_check PRIVATE
    KISAK_GATE3_COM_INIT_PREFIX=1)

set(KISAK_WEB_CANONICAL_RUNTIME_SOURCES
    "${SRC_DIR}/qcommon/common_shutdown.cpp"
    "${SRC_DIR}/client/cl_fastfile_config.cpp"
    "${SRC_DIR}/client/cl_controller.cpp"
    "${SRC_DIR}/client/cl_cin.cpp"
    "${SRC_DIR}/client/cl_initialization.cpp"
    "${SRC_DIR}/client/cl_main_pc.cpp"
    "${SRC_DIR}/database/db_auth.cpp"
    "${SRC_DIR}/database/db_file_platform.cpp"
    "${SRC_DIR}/database/db_file_load.cpp"
    "${SRC_DIR}/database/db_generated_loaders.cpp"
    "${SRC_DIR}/database/db_generated_xmodel.cpp"
    "${SRC_DIR}/database/db_generated_xanim.cpp"
    "${SRC_DIR}/database/db_generated_xmodel_surface.cpp"
    "${SRC_DIR}/database/db_generated_xmodel_physics.cpp"
    "${SRC_DIR}/database/db_generated_weapon.cpp"
    "${SRC_DIR}/database/db_generated_stringtable.cpp"
    "${SRC_DIR}/database/db_generated_comworld.cpp"
    "${SRC_DIR}/database/db_generated_gfxworld.cpp"
    "${SRC_DIR}/database/db_generated_gameworld.cpp"
    "${SRC_DIR}/database/db_generated_mapents.cpp"
    "${SRC_DIR}/database/db_generated_clipmap.cpp"
    "${SRC_DIR}/database/db_generated_image.cpp"
    "${SRC_DIR}/database/db_generated_image_platform.cpp"
    "${SRC_DIR}/database/db_generated_localize.cpp"
    "${SRC_DIR}/database/db_generated_sound_curve.cpp"
    "${SRC_DIR}/database/db_generated_sound_alias.cpp"
    "${SRC_DIR}/database/db_generated_loaded_sound.cpp"
    "${SRC_DIR}/database/db_generated_sound_platform.cpp"
    "${SRC_DIR}/database/db_generated_font.cpp"
    "${SRC_DIR}/database/db_generated_fx.cpp"
    "${SRC_DIR}/database/db_generated_fx_impact.cpp"
    "${SRC_DIR}/database/db_generated_light.cpp"
    "${SRC_DIR}/database/db_generated_menu.cpp"
    "${SRC_DIR}/database/db_generated_menu_expression.cpp"
    "${SRC_DIR}/database/db_generated_material.cpp"
    "${SRC_DIR}/database/db_generated_material_platform.cpp"
    "${SRC_DIR}/database/db_generated_material_technique.cpp"
    "${SRC_DIR}/database/db_generated_water.cpp"
    "${SRC_DIR}/database/db_semantic_trace.cpp"
    "${SRC_DIR}/database/db_memory.cpp"
    "${SRC_DIR}/database/db_registry_pools.cpp"
    "${SRC_DIR}/database/db_registry_publication.cpp"
    "${SRC_DIR}/database/db_assetnames.cpp"
    "${SRC_DIR}/database/db_registry.cpp"
    "${SRC_DIR}/database/db_runtime_prefix.cpp"
    "${SRC_DIR}/database/db_stream.cpp"
    "${SRC_DIR}/database/db_stream_load.cpp"
    "${SRC_DIR}/database/db_stringtable_load.cpp"
    "${SRC_DIR}/qcommon/iwd_archive.cpp"
    "${SRC_DIR}/EffectsCore/fx_fastfile_registration.cpp"
    "${SRC_DIR}/qcommon/iwi_image.cpp"
    "${SRC_DIR}/gfx_d3d/r_image_wavelet.cpp"
    "${SRC_DIR}/qcommon/engine_lifecycle_trace.cpp"
    "${SRC_DIR}/qcommon/graph.cpp"
    "${SRC_DIR}/qcommon/common_runtime_state.cpp"
    "${SRC_DIR}/qcommon/common_runtime_continuation.cpp"
    "${SRC_DIR}/qcommon/common_runtime_commands.cpp"
    "${SRC_DIR}/qcommon/cm_load.cpp"
    "${SRC_DIR}/qcommon/cm_memory.cpp"
    "${SRC_DIR}/qcommon/com_world_runtime.cpp"
    "${SRC_DIR}/gfx_d3d/r_text.cpp"
    "${SRC_DIR}/gfx_d3d/r_image_resample.cpp"
    "${SRC_DIR}/gfx_d3d/r_gamma.cpp"
    "${SRC_DIR}/web/web_savegame_image.cpp"
    "${SRC_DIR}/web/web_display.cpp"
    "${SRC_DIR}/gfx_d3d/r_text_cmds.cpp"
    "${SRC_DIR}/gfx_d3d/r_asset_load.cpp"
    "${SRC_DIR}/gfx_d3d/r_water.cpp"
    "${SRC_DIR}/universal/fft.cpp"
    "${SRC_DIR}/gfx_d3d/gfx_world_runtime.cpp"
    "${SRC_DIR}/ui/mission.cpp"
    "${SRC_DIR}/universal/filesystem_path.cpp"
    "${SRC_DIR}/universal/com_memory.cpp"
    "${SRC_DIR}/universal/dvar_runtime_io.cpp"
    "${SRC_DIR}/universal/memfile.cpp"
    # Existing KisakCOD/ODE physics code, not a bootstrap substitute.
    "${SRC_DIR}/physics/ode/odemath.cpp"
    $<TARGET_OBJECTS:kisak_web_runtime_prefix>
)

set(KISAK_WEB_CANONICAL_ADAPTER_SOURCES
    "${SRC_DIR}/web/web_client_server_lifecycle.cpp"
    "${SRC_DIR}/web/web_canonical_gfxworld.cpp"
    "${SRC_DIR}/web/web_renderer_frontend.cpp"
    "${SRC_DIR}/web/web_system_files.cpp"
)

set(KISAK_WEB_PLATFORM_SOURCES
    "${SRC_DIR}/web/web_browser_bindings.cpp"
    "${SRC_DIR}/web/web_assertive.cpp"
    "${SRC_DIR}/web/web_cinematic.cpp"
    "${SRC_DIR}/web/web_cinematic_decoder.cpp"
    "${SRC_DIR}/web/web_asset_probe.cpp"
    "${SRC_DIR}/web/web_system.cpp"
    "${SRC_DIR}/web/web_thread_context.cpp"
    "${SRC_DIR}/web/web_worker_filesystem.cpp"
)

set(KISAK_WEB_RENDERER_SOURCES
    "${SRC_DIR}/gfx_d3d/r_dpvs_core.cpp"
    "${SRC_DIR}/universal/com_convexhull.cpp"
    "${SRC_DIR}/web/web_renderer.cpp"
    "${SRC_DIR}/web/web_renderer_context.cpp"
    "${SRC_DIR}/web/web_renderer_code_mesh.cpp"
    "${SRC_DIR}/web/web_renderer_dobj_lod.cpp"
    "${SRC_DIR}/web/web_renderer_dobj_scene.cpp"
    "${SRC_DIR}/web/web_renderer_fx_model_scene.cpp"
    "${SRC_DIR}/web/web_renderer_lighting.cpp"
    "${SRC_DIR}/web/web_renderer_mark_fragments.cpp"
    "${SRC_DIR}/web/web_renderer_mark_mesh.cpp"
    "${SRC_DIR}/web/web_renderer_particle_cloud_scene.cpp"
    "${SRC_DIR}/web/web_renderer_static_model_scene.cpp"
    "${SRC_DIR}/web/web_renderer_surface.cpp"
    "${SRC_DIR}/web/web_renderer_world_scene.cpp"
)

set(KISAK_WEB_AUDIO_SOURCES
    "${SRC_DIR}/web/web_openal_proxy.cpp"
)

set(KISAK_WEB_PRODUCT_SOURCES
    "${SRC_DIR}/web/web_main.cpp"
)

add_executable(KisakCOD-web
    ${KISAK_WEB_CANONICAL_RUNTIME_SOURCES}
    ${KISAK_WEB_CANONICAL_ADAPTER_SOURCES}
    ${KISAK_WEB_PLATFORM_SOURCES}
    ${KISAK_WEB_RENDERER_SOURCES}
    ${KISAK_WEB_AUDIO_SOURCES}
    ${KISAK_WEB_PRODUCT_SOURCES})
add_dependencies(KisakCOD-web update_build_number)

# The post-DB runtime is no longer a browser-owned bootstrap. Pull the real SP
# game/server inventories into the production target so SV_InitGameProgs,
# G_InitGame, entity spawning, script loading, and the local connection path
# converge as one canonical link closure. Platform-owned client/cgame backends
# remain separate until this server/game closure is complete.
target_sources(KisakCOD-web PRIVATE
    ${AIM_ASSIST}
    ${BGAME}
    ${GAME}
    ${SCRIPT}
    ${SERVER}
    ${XANIM}
    ${CLIENT}
    ${DYNENTITY}
    ${DEVGUI}
    ${STRINGED}
    ${UI}
    ${CGAME}
    ${EFFECTSCORE}
    ${RAGDOLL}
    ${PHYSICS}
    ${PHYSICS_ODE}
    ${SOUND}

    # Canonical portable owners reached from G_InitGame. Keep this curated so
    # native Win32 files and already-compiled prefix sources are not admitted.
    "${SRC_DIR}/universal/com_math.cpp"
    "${SRC_DIR}/universal/com_math_anglevectors.cpp"
    "${SRC_DIR}/universal/com_loadutils.cpp"
    "${SRC_DIR}/universal/q_parse.cpp"
    "${SRC_DIR}/universal/com_sndalias.cpp"
    "${SRC_DIR}/universal/com_sndalias_curve.cpp"
    "${SRC_DIR}/universal/com_stringtable.cpp"
    "${SRC_DIR}/universal/surfaceflags.cpp"
    "${SRC_DIR}/universal/com_files.cpp"
    "${SRC_DIR}/universal/com_sndalias_load_obj.cpp"
    "${SRC_DIR}/universal/pool_allocator.cpp"
    "${SRC_DIR}/universal/profile.cpp"
    "${SRC_DIR}/universal/timing.cpp"
    "${SRC_DIR}/buildnumber.cpp"
    "${SRC_DIR}/gfx_d3d/r_fastfile_registration.cpp"
    "${SRC_DIR}/gfx_d3d/r_dvars.cpp"
    "${SRC_DIR}/gfx_d3d/r_sky.cpp"
    "${SRC_DIR}/qcommon/cm_showcollision.cpp"
    "${SRC_DIR}/qcommon/cm_mesh.cpp"
    "${SRC_DIR}/qcommon/cm_staticmodel.cpp"
    "${SRC_DIR}/qcommon/cm_test.cpp"
    "${SRC_DIR}/qcommon/cm_trace.cpp"
    "${SRC_DIR}/qcommon/cm_tracebox.cpp"
    "${SRC_DIR}/qcommon/cm_world.cpp"
    "${SRC_DIR}/qcommon/com_pack.cpp"
    "${SRC_DIR}/qcommon/com_bsp.cpp"
    "${SRC_DIR}/qcommon/com_profilemapload.cpp"
    "${SRC_DIR}/qcommon/com_checksum.cpp"
    "${SRC_DIR}/qcommon/com_fileaccess.cpp"
    "${SRC_DIR}/qcommon/com_playerprofile.cpp"
    "${SRC_DIR}/qcommon/files.cpp"
    "${SRC_DIR}/qcommon/statmonitor.cpp"
    "${SRC_DIR}/qcommon/dobj_management.cpp"
    "${SRC_DIR}/qcommon/md4.cpp"
    "${SRC_DIR}/qcommon/mem_track.cpp"
    "${SRC_DIR}/qcommon/msg.cpp"
    "${SRC_DIR}/qcommon/unzip.cpp"
)

set_source_files_properties(
    "${SRC_DIR}/game/g_main.cpp"
    "${SRC_DIR}/server/sv_game.cpp"
    PROPERTIES COMPILE_OPTIONS "-flto")

# Keep the hot DObj conversion loop and its canonical numeric helpers visible
# to the linker. Separate Wasm objects otherwise hide their non-throwing bodies,
# preventing inlining and retaining emulated-exception call boundaries per vertex.
set_source_files_properties(
    "${SRC_DIR}/web/web_renderer_dobj_scene.cpp"
    "${SRC_DIR}/web/web_renderer_lighting.cpp"
    "${SRC_DIR}/qcommon/com_pack.cpp"
    "${SRC_DIR}/universal/com_math.cpp"
    PROPERTIES COMPILE_OPTIONS "-flto")

target_include_directories(KisakCOD-web PRIVATE
    "${SRC_DIR}"
    "${DEPS_DIR}"
)

set_target_properties(KisakCOD-web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KISAK_WEB_SITE_DIR}"
)

kisak_configure_web_target(KisakCOD-web)
set(KISAK_WEB_CINEMATIC_ROOT "${CMAKE_SOURCE_DIR}/build/ffmpeg-bink")
function(kisak_link_cinematic TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE "${KISAK_WEB_CINEMATIC_ROOT}/install/include")
    target_link_libraries(${TARGET_NAME} PRIVATE
        "${KISAK_WEB_CINEMATIC_ROOT}/libavformat/libavformat.a"
        "${KISAK_WEB_CINEMATIC_ROOT}/libavcodec/libavcodec.a"
        "${KISAK_WEB_CINEMATIC_ROOT}/libavutil/libavutil.a")
endfunction()
kisak_link_cinematic(KisakCOD-web)
add_executable(KisakCOD-web-cinematic-check EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/tests/native/web_cinematic_decoder_tests.cpp"
    "${SRC_DIR}/web/web_cinematic_decoder.cpp")
target_compile_features(KisakCOD-web-cinematic-check PRIVATE cxx_std_20)
target_compile_options(KisakCOD-web-cinematic-check PRIVATE -Oz -flto -UNDEBUG)
target_include_directories(KisakCOD-web-cinematic-check PRIVATE "${SRC_DIR}")
target_link_options(KisakCOD-web-cinematic-check PRIVATE
    -Oz -flto "-sENVIRONMENT=node" "-sNODERAWFS=1" "-sALLOW_MEMORY_GROWTH=1")
set_target_properties(KisakCOD-web-cinematic-check PROPERTIES SUFFIX ".cjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cinematic-check")
kisak_link_cinematic(KisakCOD-web-cinematic-check)
target_link_options(KisakCOD-web PRIVATE "-flto" "-Wl,--error-limit=0")
target_compile_definitions(KisakCOD-web PRIVATE
    KISAK_GATE3_COM_INIT_PREFIX=1
    KISAK_CANONICAL_MAP_COMMANDS=1
    KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1
    KISAK_PORTABLE_IWI_WAVELET=1
    KISAK_OPENAL=1
    KISAK_WEB_DIAGNOSTICS=0
)
target_link_options(KisakCOD-web PRIVATE
    "-Wl,-Map=${CMAKE_BINARY_DIR}/kisakcod-production.map")

function(kisak_assert_unique_target_sources TARGET_NAME)
    get_target_property(target_sources ${TARGET_NAME} SOURCES)
    set(seen_sources)
    set(duplicate_sources)
    foreach(source IN LISTS target_sources)
        if (source MATCHES "^\\$<")
            continue()
        endif()
        cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE normalized_source)
        if (normalized_source IN_LIST seen_sources)
            list(APPEND duplicate_sources "${normalized_source}")
            continue()
        endif()
        list(APPEND seen_sources "${normalized_source}")
    endforeach()
    if (duplicate_sources)
        list(JOIN duplicate_sources "\n  " duplicate_list)
        message(FATAL_ERROR
            "${TARGET_NAME} owns duplicate translation units:\n  ${duplicate_list}")
    endif()
endfunction()

kisak_assert_unique_target_sources(KisakCOD-web)

if (KISAK_WEB_DIAGNOSTICS)
    get_target_property(KISAK_WEB_PRODUCTION_SOURCES KisakCOD-web SOURCES)
    add_executable(KisakCOD-web-diagnostics
        ${KISAK_WEB_PRODUCTION_SOURCES})
    add_dependencies(KisakCOD-web-diagnostics update_build_number)
    target_include_directories(KisakCOD-web-diagnostics PRIVATE
        "${SRC_DIR}" "${DEPS_DIR}")
    set_target_properties(KisakCOD-web-diagnostics PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/site-diagnostics")
    kisak_configure_web_target(KisakCOD-web-diagnostics DIAGNOSTICS)
    kisak_link_cinematic(KisakCOD-web-diagnostics)
    target_link_options(KisakCOD-web-diagnostics PRIVATE
        "-flto"
        "-Wl,--error-limit=0"
        "-Wl,-Map=${CMAKE_BINARY_DIR}/kisakcod-diagnostics.map")
    target_compile_definitions(KisakCOD-web-diagnostics PRIVATE
        KISAK_GATE3_COM_INIT_PREFIX=1
        KISAK_CANONICAL_MAP_COMMANDS=1
        KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1
        KISAK_PORTABLE_IWI_WAVELET=1
        KISAK_OPENAL=1
        KISAK_WEB_DIAGNOSTICS=1)
    kisak_assert_unique_target_sources(KisakCOD-web-diagnostics)
    add_custom_command(TARGET KisakCOD-web-diagnostics POST_BUILD
        BYPRODUCTS "${CMAKE_BINARY_DIR}/site-diagnostics/kisakcod.wasm"
        COMMAND "${CMAKE_COMMAND}" -E true
        VERBATIM)
endif()

# A small, strict-link inventory executable for the canonical common.cpp
# prefix. It is intentionally independent of the browser launcher and Gate 2
# runtime so accidental undefined symbols cannot hide behind that larger link.
set(KISAK_WEB_TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/tests/native/gate3_com_init_trace_tests.cpp"
    "${SRC_DIR}/universal/com_memory_string.cpp")
add_executable(KisakCOD-web-runtime-prefix-check EXCLUDE_FROM_ALL
    ${KISAK_WEB_TEST_SOURCES}
    $<TARGET_OBJECTS:kisak_web_runtime_prefix_check>
)
target_compile_features(KisakCOD-web-runtime-prefix-check PRIVATE cxx_std_20)
target_include_directories(KisakCOD-web-runtime-prefix-check PRIVATE "${SRC_DIR}" "${DEPS_DIR}")
target_compile_definitions(KisakCOD-web-runtime-prefix-check PRIVATE
    KISAK_WEB=1 KISAK_SP=1 KISAK_GATE3_COM_INIT_PREFIX=1 dNODEBUG=1)
target_compile_options(KisakCOD-web-runtime-prefix-check PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING")
target_link_options(KisakCOD-web-runtime-prefix-check PRIVATE
    "-sENVIRONMENT=node"
    "-sEXIT_RUNTIME=1"
    "-sNO_DISABLE_EXCEPTION_CATCHING"
    "-sERROR_ON_UNDEFINED_SYMBOLS=1"
    "-sALLOW_MEMORY_GROWTH=1"
)
set_target_properties(KisakCOD-web-runtime-prefix-check PROPERTIES
    OUTPUT_NAME "kisakcod-runtime-prefix-check"
    SUFFIX ".cjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-prefix-check"
)
find_program(KISAK_RUNTIME_CHECK_NODE_EXECUTABLE NAMES node node.exe REQUIRED)
add_custom_target(check-canonical-runtime-prefix
    COMMAND "${KISAK_RUNTIME_CHECK_NODE_EXECUTABLE}" "$<TARGET_FILE:KisakCOD-web-runtime-prefix-check>"
    DEPENDS KisakCOD-web-runtime-prefix-check
    VERBATIM
)

# Emscripten emits the JavaScript module as the primary linker output and the
# WebAssembly binary beside it. Tell Ninja about the second output so a deleted
# or stale .wasm file causes the target to relink instead of reporting no work.
add_custom_command(TARGET KisakCOD-web POST_BUILD
    BYPRODUCTS "${KISAK_WEB_SITE_DIR}/kisakcod.wasm"
    COMMAND "${CMAKE_COMMAND}" -E true
    VERBATIM
)

# The production site receives only the narrow product launcher and Worker
# protocol. Diagnostic launcher code and browser test controls are copied to a
# separate site when explicitly requested.
file(MAKE_DIRECTORY "${KISAK_WEB_SITE_DIR}")
file(READ "${CMAKE_SOURCE_DIR}/LICENSE" KISAK_GPL_TEXT)
file(READ "${CMAKE_SOURCE_DIR}/.tools/ffmpeg-8.0.3/COPYING.LGPLv2.1" KISAK_FFMPEG_LICENSE)
file(READ "${CMAKE_SOURCE_DIR}/build/reverb-wasm/reverb-license.txt" KISAK_OPENAL_LICENSE)
file(WRITE "${KISAK_WEB_SITE_DIR}/licenses.txt"
    "KisakCOD: GPL-3.0. FFmpeg 8.0.3: LGPL-2.1-or-later, copyright the FFmpeg contributors. OpenAL Soft 1.25.2: LGPL-2.0-or-later, copyright the OpenAL Soft contributors.\nCorresponding sources and build instructions: docs/cinematic-codec.md and docs/browser-reverb.md in this distribution's source.\n\n${KISAK_GPL_TEXT}\n\nFFmpeg license:\n${KISAK_FFMPEG_LICENSE}\n\nOpenAL Soft license:\n${KISAK_OPENAL_LICENSE}")
configure_file("${CMAKE_SOURCE_DIR}/build/reverb-wasm/reverb_dsp.mjs"
    "${KISAK_WEB_SITE_DIR}/reverb_dsp.mjs" COPYONLY)
file(REMOVE
    "${KISAK_WEB_SITE_DIR}/engine_worker_host.mjs"
    "${KISAK_WEB_SITE_DIR}/engine_protocol.mjs"
    "${KISAK_WEB_SITE_DIR}/filesystem_bridge.mjs"
    "${KISAK_WEB_SITE_DIR}/kisakcod-next.mjs"
    "${KISAK_WEB_SITE_DIR}/kisakcod-next.wasm")
foreach(WEB_FILE asset_profile.mjs asset_store.mjs browser_capabilities.mjs
        capability_probe_worker.mjs input_controller_core.mjs web_audio_driver.mjs web_reverb_worklet.mjs
        worker_sync_filesystem.mjs browser_quit.mjs styles.css)
    configure_file(
        "${KISAK_WEB_SOURCE_DIR}/${WEB_FILE}"
        "${KISAK_WEB_SITE_DIR}/${WEB_FILE}"
        COPYONLY
    )
endforeach()
configure_file("${KISAK_WEB_SOURCE_DIR}/product_index.html"
    "${KISAK_WEB_SITE_DIR}/index.html" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_launcher.mjs"
    "${KISAK_WEB_SITE_DIR}/launcher.mjs" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_checkpoint_controller.mjs"
    "${KISAK_WEB_SITE_DIR}/product_checkpoint_controller.mjs" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_mount_controller.mjs"
    "${KISAK_WEB_SITE_DIR}/product_mount_controller.mjs" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_engine_worker.mjs"
    "${KISAK_WEB_SITE_DIR}/engine_worker.mjs" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_engine_worker_host.mjs"
    "${KISAK_WEB_SITE_DIR}/product_engine_worker_host.mjs" COPYONLY)
configure_file("${KISAK_WEB_SOURCE_DIR}/product_protocol.mjs"
    "${KISAK_WEB_SITE_DIR}/product_protocol.mjs" COPYONLY)

configure_file("${KISAK_WEB_SOURCE_DIR}/worker_transport.mjs"
    "${KISAK_WEB_SITE_DIR}/worker_transport.mjs" COPYONLY)

if (KISAK_WEB_DIAGNOSTICS)
    set(KISAK_WEB_DIAGNOSTIC_SITE_DIR "${CMAKE_BINARY_DIR}/site-diagnostics")
    file(MAKE_DIRECTORY "${KISAK_WEB_DIAGNOSTIC_SITE_DIR}")
    configure_file("${CMAKE_SOURCE_DIR}/build/reverb-wasm/reverb_dsp.mjs"
        "${KISAK_WEB_DIAGNOSTIC_SITE_DIR}/reverb_dsp.mjs" COPYONLY)
    foreach(WEB_FILE index.html launcher.mjs asset_profile.mjs asset_store.mjs input_controller_core.mjs
            engine_worker.mjs engine_worker_host.mjs engine_protocol.mjs
            web_audio_driver.mjs web_reverb_worklet.mjs worker_sync_filesystem.mjs worker_transport.mjs browser_quit.mjs styles.css)
        configure_file(
            "${KISAK_WEB_SOURCE_DIR}/${WEB_FILE}"
            "${KISAK_WEB_DIAGNOSTIC_SITE_DIR}/${WEB_FILE}"
            COPYONLY)
    endforeach()
    file(REMOVE "${KISAK_WEB_DIAGNOSTIC_SITE_DIR}/filesystem_bridge.mjs")
endif()
