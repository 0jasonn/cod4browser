cmake_minimum_required(VERSION 3.16)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KisakCODPortableTests LANGUAGES CXX)
endif()

include(CTest)

option(KISAK_BUILD_FUZZERS "Build the native imported-asset parser fuzz target" OFF)

get_filename_component(KISAK_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(iwd_archive_tests
    "${CMAKE_CURRENT_LIST_DIR}/iwd_archive_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwd_archive.cpp"
)

add_executable(iwi_image_tests
    "${CMAKE_CURRENT_LIST_DIR}/iwi_image_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/qcommon/iwi_image.cpp"
)

add_executable(canonical_asset_abi_tests
    "${CMAKE_CURRENT_LIST_DIR}/canonical_asset_abi_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/database/db_semantic_trace.cpp"
)

add_executable(web_renderer_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_surface.cpp"
)

add_executable(web_engine_world_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_engine_world_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_engine_xmodel_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_engine_xmodel_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_engine_xmodel_material_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_engine_xmodel_material_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_material.cpp"
)

add_executable(web_engine_xmodel_draw_list_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_engine_xmodel_draw_list_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_draw_list.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_material.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_fastfile_world_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_world_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_source_stream.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_registry.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_world_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_fastfile_zone_stream_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_zone_stream_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
)

add_executable(web_fastfile_source_stream_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_source_stream_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_source_stream.cpp"
)

add_executable(web_fastfile_zone_registry_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_zone_registry_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_registry.cpp"
)

add_executable(web_asset_probe_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_asset_probe_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_asset_probe.cpp"
)

add_executable(web_qcommon_preinit_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_qcommon_preinit_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_qcommon_preinit.cpp"
)

add_executable(web_cooperative_scheduler_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_cooperative_scheduler_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_cooperative_scheduler.cpp"
)

add_executable(web_retail_fastfile_census_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_retail_fastfile_census_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/database/db_semantic_trace.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_source_stream.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_registry.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_retail_fastfile_census.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_material.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_xmodel_draw_list.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_shader_compatibility.cpp"
)

target_compile_features(iwd_archive_tests PRIVATE cxx_std_20)
target_include_directories(iwd_archive_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(iwi_image_tests PRIVATE cxx_std_20)
target_include_directories(iwi_image_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(canonical_asset_abi_tests PRIVATE cxx_std_20)
target_include_directories(canonical_asset_abi_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_engine_world_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_engine_world_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_engine_xmodel_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_engine_xmodel_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_engine_xmodel_material_tests PRIVATE cxx_std_20)
target_include_directories(web_engine_xmodel_material_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_engine_xmodel_draw_list_tests PRIVATE cxx_std_20)
target_include_directories(web_engine_xmodel_draw_list_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_world_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_world_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_zone_stream_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_zone_stream_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_source_stream_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_source_stream_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_zone_registry_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_zone_registry_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_asset_probe_tests PRIVATE cxx_std_20)
target_include_directories(web_asset_probe_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_qcommon_preinit_tests PRIVATE cxx_std_20)
target_include_directories(web_qcommon_preinit_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_cooperative_scheduler_tests PRIVATE cxx_std_20)
target_include_directories(web_cooperative_scheduler_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_retail_fastfile_census_tests PRIVATE cxx_std_20)
target_include_directories(web_retail_fastfile_census_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")

if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Use Emscripten's maintained zlib port, matching the browser target.
    target_compile_options(iwd_archive_tests PRIVATE "-sUSE_ZLIB=1")
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
    )
    target_link_options(canonical_asset_abi_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_renderer_surface_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_engine_world_surface_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_engine_xmodel_surface_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_engine_xmodel_material_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_engine_xmodel_draw_list_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_compile_options(web_fastfile_world_surface_tests PRIVATE "-sUSE_ZLIB=1")
    target_link_options(web_fastfile_world_surface_tests PRIVATE
        "-sUSE_ZLIB=1"
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_fastfile_zone_stream_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_fastfile_source_stream_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_fastfile_zone_registry_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_asset_probe_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_qcommon_preinit_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_link_options(web_cooperative_scheduler_tests PRIVATE
        "-sENVIRONMENT=node"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    target_compile_options(web_retail_fastfile_census_tests PRIVATE "-sUSE_ZLIB=1")
    target_link_options(web_retail_fastfile_census_tests PRIVATE
        "-sUSE_ZLIB=1"
        "-sENVIRONMENT=node"
        "-sNODERAWFS=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
    )
    set_target_properties(iwd_archive_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(iwi_image_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(canonical_asset_abi_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_renderer_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_engine_world_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_engine_xmodel_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_engine_xmodel_material_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_engine_xmodel_draw_list_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_world_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_zone_stream_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_source_stream_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_zone_registry_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_asset_probe_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_qcommon_preinit_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_cooperative_scheduler_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_retail_fastfile_census_tests PROPERTIES SUFFIX ".cjs")
    find_program(KISAK_NODE_EXECUTABLE NAMES node node.exe REQUIRED)
    add_test(NAME iwd_archive_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:iwd_archive_tests>")
    add_test(NAME iwi_image_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:iwi_image_tests>")
    add_test(NAME canonical_asset_abi_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:canonical_asset_abi_tests>")
    add_test(NAME web_renderer_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_renderer_surface_tests>")
    add_test(NAME web_engine_world_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_engine_world_surface_tests>")
    add_test(NAME web_engine_xmodel_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_engine_xmodel_surface_tests>")
    add_test(NAME web_engine_xmodel_material_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_engine_xmodel_material_tests>")
    add_test(NAME web_engine_xmodel_draw_list_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_engine_xmodel_draw_list_tests>")
    add_test(NAME web_fastfile_world_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_world_surface_tests>")
    add_test(NAME web_fastfile_zone_stream_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_zone_stream_tests>")
    add_test(NAME web_fastfile_source_stream_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_source_stream_tests>")
    add_test(NAME web_fastfile_zone_registry_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_zone_registry_tests>")
    add_test(NAME web_asset_probe_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_asset_probe_tests>")
    add_test(NAME web_qcommon_preinit_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_qcommon_preinit_tests>")
    add_test(NAME web_cooperative_scheduler_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_cooperative_scheduler_tests>")
    add_test(NAME web_retail_fastfile_census_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_retail_fastfile_census_tests>")
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
    target_link_libraries(web_fastfile_world_surface_tests PRIVATE ${KISAK_TEST_ZLIB_TARGET})
    target_link_libraries(web_retail_fastfile_census_tests PRIVATE ${KISAK_TEST_ZLIB_TARGET})
    add_test(NAME iwd_archive_tests COMMAND iwd_archive_tests)
    add_test(NAME iwi_image_tests COMMAND iwi_image_tests)
    add_test(NAME canonical_asset_abi_tests COMMAND canonical_asset_abi_tests)
    add_test(NAME web_renderer_surface_tests COMMAND web_renderer_surface_tests)
    add_test(NAME web_engine_world_surface_tests COMMAND web_engine_world_surface_tests)
    add_test(NAME web_engine_xmodel_surface_tests COMMAND web_engine_xmodel_surface_tests)
    add_test(NAME web_engine_xmodel_material_tests COMMAND web_engine_xmodel_material_tests)
    add_test(NAME web_engine_xmodel_draw_list_tests COMMAND web_engine_xmodel_draw_list_tests)
    add_test(NAME web_fastfile_world_surface_tests COMMAND web_fastfile_world_surface_tests)
    add_test(NAME web_fastfile_zone_stream_tests COMMAND web_fastfile_zone_stream_tests)
    add_test(NAME web_fastfile_source_stream_tests COMMAND web_fastfile_source_stream_tests)
    add_test(NAME web_fastfile_zone_registry_tests COMMAND web_fastfile_zone_registry_tests)
    add_test(NAME web_asset_probe_tests COMMAND web_asset_probe_tests)
    add_test(NAME web_qcommon_preinit_tests COMMAND web_qcommon_preinit_tests)
    add_test(NAME web_cooperative_scheduler_tests COMMAND web_cooperative_scheduler_tests)
    add_test(NAME web_retail_fastfile_census_tests COMMAND web_retail_fastfile_census_tests)
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
        "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_source_stream.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_registry.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_world_surface.cpp"
        "${KISAK_REPOSITORY_ROOT}/src/web/web_shader_compatibility.cpp"
    )
    target_compile_features(asset_parsers_fuzz PRIVATE cxx_std_20)
    target_include_directories(asset_parsers_fuzz PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
    target_compile_options(asset_parsers_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(asset_parsers_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
    target_link_libraries(asset_parsers_fuzz PRIVATE ZLIB::ZLIB)
endif()
