message("----- KisakCOD Browser Platform Slice -----")

project(KisakCOD-web LANGUAGES C CXX)

include(${PLATFORM_DIR}/web/platform.cmake)
include(${SCRIPTS_DIR}/common_files.cmake)
include(${SCRIPTS_DIR}/sp/sp_files.cmake)

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

# These sources were pulled in explicitly while the runtime closure was still
# narrow.  The complete inventories below now own them; keep one translation
# unit per canonical implementation.
list(REMOVE_ITEM CGAME
    "${SRC_DIR}/cgame/cg_ents.cpp"
    "${SRC_DIR}/cgame/cg_weapons.cpp"
    "${SRC_DIR}/cgame/cg_view.cpp"
    "${SRC_DIR}/cgame/cg_world.cpp")
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

# Compile the canonical qcommon/runtime prefix once. The production Worker and
# the strict Node runtime check intentionally link the same Wasm objects; only
# their entry points and link environments differ.
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

# Gate 2 remains linked for an explicitly requested diagnostic/oracle run. Keep
# its large census implementation isolated so a future target can omit this
# library once canonical DB loading reproduces the proof.
add_library(kisak_web_gate2_oracle STATIC
    "${SRC_DIR}/web/web_retail_fastfile_census.cpp"
    "${SRC_DIR}/web/web_retail_load_clipmap.cpp"
    "${SRC_DIR}/web/web_retail_load_comworld.cpp"
    "${SRC_DIR}/web/web_retail_load_gfxworld.cpp"
    "${SRC_DIR}/web/web_retail_load_image.cpp"
    "${SRC_DIR}/web/web_retail_load_lightdef.cpp"
    "${SRC_DIR}/web/web_retail_load_weapon.cpp"
    "${SRC_DIR}/web/web_sound_alias_catalog.cpp"
    "${SRC_DIR}/web/web_retail_census_job.cpp"
)
target_include_directories(kisak_web_gate2_oracle PRIVATE "${SRC_DIR}" "${DEPS_DIR}")
kisak_configure_web_compile_target(kisak_web_gate2_oracle)

add_executable(KisakCOD-web
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
    "${SRC_DIR}/database/db_registry.cpp"
    "${SRC_DIR}/database/db_runtime_prefix.cpp"
    "${SRC_DIR}/database/db_stream.cpp"
    "${SRC_DIR}/database/db_stream_load.cpp"
    "${SRC_DIR}/database/db_stringtable_load.cpp"
    "${SRC_DIR}/qcommon/iwd_archive.cpp"
    "${SRC_DIR}/EffectsCore/fx_fastfile_registration.cpp"
    "${SRC_DIR}/qcommon/iwi_image.cpp"
    "${SRC_DIR}/qcommon/engine_lifecycle_trace.cpp"
    "${SRC_DIR}/qcommon/graph.cpp"
    "${SRC_DIR}/qcommon/common_runtime_state.cpp"
    "${SRC_DIR}/qcommon/common_runtime_continuation.cpp"
    "${SRC_DIR}/qcommon/cm_load.cpp"
    "${SRC_DIR}/qcommon/cm_memory.cpp"
    "${SRC_DIR}/qcommon/com_world_runtime.cpp"
    "${SRC_DIR}/script/scr_variable_init.cpp"
    "${SRC_DIR}/script/scr_vm_init.cpp"
    "${SRC_DIR}/xanim/xanim_init.cpp"
    "${SRC_DIR}/xanim/dobj_init.cpp"
    "${SRC_DIR}/gfx_d3d/r_asset_load.cpp"
    "${SRC_DIR}/gfx_d3d/gfx_world_runtime.cpp"
    "${SRC_DIR}/game/savememory.cpp"
    "${SRC_DIR}/game/savememory_init.cpp"
    "${SRC_DIR}/game/g_main.cpp"
    "${SRC_DIR}/server/sv_init.cpp"
    "${SRC_DIR}/server/sv_game.cpp"
    "${SRC_DIR}/server/sv_map_assets.cpp"
    "${SRC_DIR}/server/sv_map_command.cpp"
    "${SRC_DIR}/server/sv_state.cpp"
    "${SRC_DIR}/ui/mission.cpp"
    "${SRC_DIR}/universal/filesystem_path.cpp"
    "${SRC_DIR}/universal/com_memory.cpp"
    "${SRC_DIR}/universal/dvar_runtime_io.cpp"
    "${SRC_DIR}/universal/memfile.cpp"
    "${SRC_DIR}/web/web_archive_job.cpp"
    "${SRC_DIR}/web/web_client_server_lifecycle.cpp"
    "${SRC_DIR}/web/web_canonical_gfxworld.cpp"
    "${SRC_DIR}/web/web_assertive.cpp"
    "${SRC_DIR}/web/web_cinematic.cpp"
    "${SRC_DIR}/web/web_asset_probe.cpp"
    "${SRC_DIR}/web/web_cooperative_scheduler.cpp"
    "${SRC_DIR}/web/web_database_filesystem.cpp"
    "${SRC_DIR}/web/web_engine_asset.cpp"
    "${SRC_DIR}/web/web_engine_filesystem.cpp"
    "${SRC_DIR}/web/web_engine_scheduler.cpp"
    "${SRC_DIR}/web/web_engine_surface.cpp"
    "${SRC_DIR}/web/web_engine_world_surface.cpp"
    "${SRC_DIR}/web/web_fastfile_world_surface.cpp"
    "${SRC_DIR}/web/web_fastfile_source_stream.cpp"
    "${SRC_DIR}/web/web_fastfile_zone_registry.cpp"
    "${SRC_DIR}/web/web_fastfile_zone_stream.cpp"
    "${SRC_DIR}/web/web_filesystem.cpp"
    "${SRC_DIR}/web/web_qcommon_preinit.cpp"
    "${SRC_DIR}/web/web_qcommon_runtime.cpp"
    "${SRC_DIR}/web/web_shader_compatibility.cpp"
    "${SRC_DIR}/web/web_renderer.cpp"
      "${SRC_DIR}/web/web_renderer_frontend.cpp"
      "${SRC_DIR}/web/web_renderer_dobj_scene.cpp"
      "${SRC_DIR}/web/web_renderer_surface.cpp"
    "${SRC_DIR}/web/web_renderer_world_scene.cpp"
    "${SRC_DIR}/web/web_renderer_static_model_scene.cpp"
    "${SRC_DIR}/web/web_system.cpp"
    "${SRC_DIR}/web/web_system_files.cpp"
    "${SRC_DIR}/web/web_thread_context.cpp"
    "${SRC_DIR}/web/web_worker_filesystem.cpp"
    "${SRC_DIR}/web/web_main.cpp"
    # This is existing KisakCOD/ODE physics code, not a bootstrap substitute.
    "${SRC_DIR}/physics/ode/odemath.cpp"
    $<TARGET_OBJECTS:kisak_web_runtime_prefix>
)
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

    # These four were removed from CGAME above because they entered the target
    # during the first narrow cgame closure.
    "${SRC_DIR}/cgame/cg_ents.cpp"
    "${SRC_DIR}/cgame/cg_weapons.cpp"
    "${SRC_DIR}/cgame/cg_view.cpp"
    "${SRC_DIR}/cgame/cg_world.cpp"

    # Canonical portable owners reached from G_InitGame. Keep this curated so
    # native Win32 files and already-compiled prefix sources are not admitted.
    "${SRC_DIR}/universal/com_math.cpp"
    "${SRC_DIR}/universal/com_math_anglevectors.cpp"
    "${SRC_DIR}/universal/com_loadutils.cpp"
    "${SRC_DIR}/universal/q_parse.cpp"
    "${SRC_DIR}/universal/com_sndalias.cpp"
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

target_include_directories(KisakCOD-web PRIVATE
    "${SRC_DIR}"
    "${DEPS_DIR}"
)

set_target_properties(KisakCOD-web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KISAK_WEB_SITE_DIR}"
)

kisak_configure_web_target(KisakCOD-web)
target_link_options(KisakCOD-web PRIVATE "-flto" "-Wl,--error-limit=0")
target_compile_definitions(KisakCOD-web PRIVATE
    KISAK_GATE3_COM_INIT_PREFIX=1
    KISAK_CANONICAL_MAP_COMMANDS=1
    KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1
    KISAK_OPENAL=1
)
target_link_libraries(KisakCOD-web PRIVATE kisak_web_gate2_oracle)

# A small, strict-link inventory executable for the canonical common.cpp
# prefix. It is intentionally independent of the browser launcher and Gate 2
# runtime so accidental undefined symbols cannot hide behind that larger link.
add_executable(KisakCOD-web-runtime-prefix-check EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/tests/native/gate3_com_init_trace_tests.cpp"
    "${SRC_DIR}/universal/com_memory_string.cpp"
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

# configure_file registers these launcher files as configure dependencies, so
# editing one refreshes the served site on the next incremental build.
file(MAKE_DIRECTORY "${KISAK_WEB_SITE_DIR}")
foreach(WEB_FILE index.html launcher.mjs asset_store.mjs filesystem_bridge.mjs
        engine_worker.mjs engine_worker_host.mjs worker_sync_filesystem.mjs styles.css)
    configure_file(
        "${KISAK_WEB_SOURCE_DIR}/${WEB_FILE}"
        "${KISAK_WEB_SITE_DIR}/${WEB_FILE}"
        COPYONLY
    )
endforeach()
