if (NOT KISAK_PLATFORM STREQUAL "web")
    message(FATAL_ERROR "The web platform configuration requires KISAK_PLATFORM=web")
endif()

if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR "The web platform configuration requires Emscripten")
endif()

function(kisak_configure_web_compile_target TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    # Platform and engine identity are orthogonal. Web must never inherit the
    # Win32 host merely to select the canonical offline single-player code.
    target_compile_definitions(${TARGET_NAME} PRIVATE KISAK_WEB=1 KISAK_SP=1 dNODEBUG=1)
    target_compile_options(${TARGET_NAME} PRIVATE
        "-sUSE_ZLIB=1"
        # Imported metadata controls several bounded allocations. Keep the
        # explicit bad_alloc recovery paths live in production builds.
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Debug>:-g3>"
        "$<$<NOT:$<CONFIG:Debug>>:-O2>"
    )
endfunction()

function(kisak_configure_web_target TARGET_NAME)
    kisak_configure_web_compile_target(${TARGET_NAME})

    target_link_options(${TARGET_NAME} PRIVATE
        "-sUSE_ZLIB=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=worker"
        "-sOFFSCREENCANVAS_SUPPORT=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sERROR_ON_UNDEFINED_SYMBOLS=1"
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_KisakWeb_ProbeLocalization,_KisakWeb_ProbeIwd,_KisakWeb_ProbeFastfileHeader,_KisakWeb_CompleteFsStat,_KisakWeb_CompleteFsRead,_KisakWeb_StartArchiveJob,_KisakWeb_CancelArchiveJob,_KisakWeb_StartQcommonRuntime,_KisakWeb_CancelQcommonRuntime,_KisakWeb_StartRetailCensus,_KisakWeb_CancelRetailCensus,_KisakWeb_StartCanonicalDbRuntimeCheck,_KisakWeb_TestLoseWebGLContext,_KisakWeb_TestRestoreWebGLContext"
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8"
        "$<$<CONFIG:Debug>:-sASSERTIONS=2>"
        "$<$<CONFIG:Debug>:-sGL_ASSERTIONS=1>"
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "kisakcod"
        SUFFIX ".mjs"
    )
endfunction()
