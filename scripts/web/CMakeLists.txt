message("----- KisakCOD Browser Platform Slice -----")

project(KisakCOD-web LANGUAGES C CXX)

include(${PLATFORM_DIR}/web/platform.cmake)

set(KISAK_WEB_SITE_DIR "${CMAKE_BINARY_DIR}/site")
set(KISAK_WEB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/web")

add_executable(KisakCOD-web
    # The first platform-neutral qcommon slice: command parsing/buffering and
    # an owned-string dvar registry with no filesystem or Win32 dependencies.
    "${SRC_DIR}/qcommon/cmd_core.cpp"
    "${SRC_DIR}/qcommon/iwd_archive.cpp"
    "${SRC_DIR}/qcommon/iwi_image.cpp"
    "${SRC_DIR}/universal/dvar_core.cpp"
    "${SRC_DIR}/web/web_archive_job.cpp"
    "${SRC_DIR}/web/web_asset_probe.cpp"
    "${SRC_DIR}/web/web_engine_asset.cpp"
    "${SRC_DIR}/web/web_engine_filesystem.cpp"
    "${SRC_DIR}/web/web_engine_surface.cpp"
    "${SRC_DIR}/web/web_engine_world_surface.cpp"
    "${SRC_DIR}/web/web_fastfile_world_surface.cpp"
    "${SRC_DIR}/web/web_fastfile_zone_stream.cpp"
    "${SRC_DIR}/web/web_filesystem.cpp"
    "${SRC_DIR}/web/web_renderer.cpp"
    "${SRC_DIR}/web/web_renderer_surface.cpp"
    "${SRC_DIR}/web/web_system.cpp"
    "${SRC_DIR}/web/web_main.cpp"
    # This is existing KisakCOD/ODE physics code, not a bootstrap substitute.
    "${SRC_DIR}/physics/ode/odemath.cpp"
)

target_include_directories(KisakCOD-web PRIVATE
    "${SRC_DIR}"
    "${DEPS_DIR}"
)

set_target_properties(KisakCOD-web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KISAK_WEB_SITE_DIR}"
)

kisak_configure_web_target(KisakCOD-web)

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
foreach(WEB_FILE index.html launcher.mjs asset_store.mjs filesystem_bridge.mjs styles.css)
    configure_file(
        "${KISAK_WEB_SOURCE_DIR}/${WEB_FILE}"
        "${KISAK_WEB_SITE_DIR}/${WEB_FILE}"
        COPYONLY
    )
endforeach()
