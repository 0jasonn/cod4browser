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
    target_compile_definitions(${TARGET_NAME} PRIVATE
        KISAK_WEB=1 KISAK_SP=1 dNODEBUG=1
        # MSVC integral spellings occur in the bundled ODE sources before an
        # engine header can provide the usual compatibility macros.
        __int64=long\ long __int32=int __int16=short __int8=char)
    target_compile_options(${TARGET_NAME} PRIVATE
        "-fdeclspec"
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

    set(KISAK_WEB_EXPORTED_FUNCTIONS
        "_main,_malloc,_free,_KisakWeb_ProbeLocalization,_KisakWeb_ProbeIwd,_KisakWeb_ProbeFastfileHeader,_KisakWeb_CompleteFsStat,_KisakWeb_CompleteFsRead,_KisakWeb_MountCanonicalRuntime,_KisakWeb_SubmitCanonicalCommand,_KisakWeb_QueueKeyEvent,_KisakWeb_QueueMouseMove")
    if (ARGV1 STREQUAL "DIAGNOSTICS")
        set(KISAK_WEB_EXPORTED_FUNCTIONS
            "${KISAK_WEB_EXPORTED_FUNCTIONS},_KisakWeb_CanonicalFsFileSize,_KisakWeb_CanonicalFsListCount,_KisakWeb_CanonicalFsReadHash,_KisakWeb_CanonicalFsWriteRename,_KisakWeb_DiagnosticCinematicOmission,_KisakWeb_TestAudioProxyPcm,_KisakWeb_TestLoseWebGLContext,_KisakWeb_TestRestoreWebGLContext,_KisakWeb_TestSetAaSamples,_KisakWeb_TestSubmitSurface,_KisakWeb_TestSlowNextCommand,_KisakWeb_TestUiState,_KisakWeb_TestMenuState,_KisakWeb_TestResumeGame,_KisakWeb_TestObjectiveNotification,_KisakWeb_TestUiTextSeen,_KisakWeb_TestConfigState,_KisakWeb_TestProfileState,_KisakWeb_TestSaveState")
    endif()

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
        # Canonical map/save load nests substantially deeper than Emscripten's
        # 64 KiB default. Match the native Windows stack scale at the platform
        # boundary instead of rewriting shared engine call chains.
        "-sSTACK_SIZE=1048576"
        "-sEXIT_RUNTIME=0"
        "-sERROR_ON_UNDEFINED_SYMBOLS=1"
        "-sEXPORTED_FUNCTIONS=${KISAK_WEB_EXPORTED_FUNCTIONS}"
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8"
        "$<$<CONFIG:Debug>:-sASSERTIONS=2>"
        "$<$<CONFIG:Debug>:-sGL_ASSERTIONS=1>"
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "kisakcod"
        SUFFIX ".mjs"
    )
endfunction()
