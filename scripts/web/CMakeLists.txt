message("----- KisakCOD Browser Platform Slice -----")

project(KisakCOD-web LANGUAGES C CXX)

include(${PLATFORM_DIR}/web/platform.cmake)

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
    "${SRC_DIR}/universal/com_memory_string.cpp"
    "${SRC_DIR}/universal/com_shared.cpp"
    "${SRC_DIR}/universal/dvar.cpp"
    "${SRC_DIR}/universal/dvar_cmds.cpp"
    "${SRC_DIR}/universal/q_shared.cpp"
    "${SRC_DIR}/universal/physicalmemory.cpp"
)
add_library(kisak_web_runtime_prefix OBJECT ${KISAK_WEB_RUNTIME_PREFIX_SOURCES})
target_include_directories(kisak_web_runtime_prefix PRIVATE "${SRC_DIR}" "${DEPS_DIR}")
kisak_configure_web_compile_target(kisak_web_runtime_prefix)
target_compile_definitions(kisak_web_runtime_prefix PRIVATE KISAK_GATE3_COM_INIT_PREFIX=1)

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
    "${SRC_DIR}/database/db_auth.cpp"
    "${SRC_DIR}/database/db_file_platform.cpp"
    "${SRC_DIR}/database/db_file_load.cpp"
    "${SRC_DIR}/database/db_generated_loaders.cpp"
    "${SRC_DIR}/database/db_generated_image.cpp"
    "${SRC_DIR}/database/db_generated_image_platform.cpp"
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
    "${SRC_DIR}/qcommon/iwd_archive.cpp"
    "${SRC_DIR}/qcommon/iwi_image.cpp"
    "${SRC_DIR}/web/web_archive_job.cpp"
    "${SRC_DIR}/web/web_assertive.cpp"
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
    "${SRC_DIR}/web/web_renderer_surface.cpp"
    "${SRC_DIR}/web/web_system.cpp"
    "${SRC_DIR}/web/web_thread_context.cpp"
    "${SRC_DIR}/web/web_main.cpp"
    # This is existing KisakCOD/ODE physics code, not a bootstrap substitute.
    "${SRC_DIR}/physics/ode/odemath.cpp"
    $<TARGET_OBJECTS:kisak_web_runtime_prefix>
)

target_include_directories(KisakCOD-web PRIVATE
    "${SRC_DIR}"
    "${DEPS_DIR}"
)

set_target_properties(KisakCOD-web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KISAK_WEB_SITE_DIR}"
)

kisak_configure_web_target(KisakCOD-web)
target_compile_definitions(KisakCOD-web PRIVATE
    KISAK_GATE3_COM_INIT_PREFIX=1
    KISAK_DB_REGISTRY_LIFECYCLE_SLICE=1
)
target_link_libraries(KisakCOD-web PRIVATE kisak_web_gate2_oracle)

# A small, strict-link inventory executable for the canonical common.cpp
# prefix. It is intentionally independent of the browser launcher and Gate 2
# runtime so accidental undefined symbols cannot hide behind that larger link.
add_executable(KisakCOD-web-runtime-prefix-check EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/tests/native/gate3_com_init_trace_tests.cpp"
    $<TARGET_OBJECTS:kisak_web_runtime_prefix>
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
