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

add_executable(web_renderer_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_renderer_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_renderer_surface.cpp"
)

add_executable(web_engine_world_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_engine_world_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_fastfile_world_surface_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_world_surface_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_world_surface.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_engine_world_surface.cpp"
)

add_executable(web_fastfile_zone_stream_tests
    "${CMAKE_CURRENT_LIST_DIR}/web_fastfile_zone_stream_tests.cpp"
    "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_zone_stream.cpp"
)

target_compile_features(iwd_archive_tests PRIVATE cxx_std_20)
target_include_directories(iwd_archive_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(iwi_image_tests PRIVATE cxx_std_20)
target_include_directories(iwi_image_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_renderer_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_renderer_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_engine_world_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_engine_world_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_world_surface_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_world_surface_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")
target_compile_features(web_fastfile_zone_stream_tests PRIVATE cxx_std_20)
target_include_directories(web_fastfile_zone_stream_tests PRIVATE "${KISAK_REPOSITORY_ROOT}/src")

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
    set_target_properties(iwd_archive_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(iwi_image_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_renderer_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_engine_world_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_world_surface_tests PROPERTIES SUFFIX ".cjs")
    set_target_properties(web_fastfile_zone_stream_tests PROPERTIES SUFFIX ".cjs")
    find_program(KISAK_NODE_EXECUTABLE NAMES node node.exe REQUIRED)
    add_test(NAME iwd_archive_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:iwd_archive_tests>")
    add_test(NAME iwi_image_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:iwi_image_tests>")
    add_test(NAME web_renderer_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_renderer_surface_tests>")
    add_test(NAME web_engine_world_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_engine_world_surface_tests>")
    add_test(NAME web_fastfile_world_surface_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_world_surface_tests>")
    add_test(NAME web_fastfile_zone_stream_tests
        COMMAND "${KISAK_NODE_EXECUTABLE}" "$<TARGET_FILE:web_fastfile_zone_stream_tests>")
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(iwd_archive_tests PRIVATE ZLIB::ZLIB)
    target_link_libraries(web_fastfile_world_surface_tests PRIVATE ZLIB::ZLIB)
    add_test(NAME iwd_archive_tests COMMAND iwd_archive_tests)
    add_test(NAME iwi_image_tests COMMAND iwi_image_tests)
    add_test(NAME web_renderer_surface_tests COMMAND web_renderer_surface_tests)
    add_test(NAME web_engine_world_surface_tests COMMAND web_engine_world_surface_tests)
    add_test(NAME web_fastfile_world_surface_tests COMMAND web_fastfile_world_surface_tests)
    add_test(NAME web_fastfile_zone_stream_tests COMMAND web_fastfile_zone_stream_tests)
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
        "${KISAK_REPOSITORY_ROOT}/src/web/web_fastfile_world_surface.cpp"
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
