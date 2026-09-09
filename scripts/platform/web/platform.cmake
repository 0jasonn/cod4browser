if (NOT KISAK_PLATFORM STREQUAL "web")
    message(FATAL_ERROR "The web platform configuration requires KISAK_PLATFORM=web")
endif()

if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR "The web platform configuration requires Emscripten")
endif()

# Matched runtime measurements selected O2/full LTO; alternatives affect only
# the engine web targets. See docs/renderer-retained-resources.md.
set(KISAK_WEB_OPTIMIZATION "O2" CACHE STRING "Web Release compile and link optimization")
set_property(CACHE KISAK_WEB_OPTIMIZATION PROPERTY STRINGS Oz O2 O3)
if (NOT KISAK_WEB_OPTIMIZATION MATCHES "^(Oz|O2|O3)$")
    message(FATAL_ERROR "KISAK_WEB_OPTIMIZATION must be Oz, O2 or O3")
endif()
option(KISAK_WEB_BATCH_DYNAMIC_UPLOAD_ERRORS "Check staged dynamic geometry and lighting uploads together" ON)
option(KISAK_WEB_SKIP_FLOATZ_LIGHTING "Skip lighting state unused by the FloatZ shader path" ON)
option(KISAK_WEB_FULL_LTO "Compile all web engine units with LTO" ON)
option(KISAK_WEB_SIMD "Enable ordinary Wasm SIMD without relaxed floating point" OFF)
option(KISAK_WEB_REUSE_WORLD_STATIC_STATE "Reuse identical world/static draw state" ON)
option(KISAK_WEB_SUPPRESS_AUDIO_EQUALITY "Suppress unchanged OpenAL source parameters" ON)
option(KISAK_WEB_SKIP_UNUSED_SHADOW_BOUNDS "Skip dynamic bounds unused by shadow partitions" ON)
option(KISAK_WEB_BATCH_SHADOW_ERRORS "Check shadow draws once per readiness group" ON)
option(KISAK_WEB_DIRECT_CLOUD_APPEND "Expand particle clouds directly into the assembled scene" ON)

function(kisak_configure_web_compile_target TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    # Platform and engine identity are orthogonal. Web must never inherit the
    # Win32 host merely to select the canonical offline single-player code.
    target_compile_definitions(${TARGET_NAME} PRIVATE
        KISAK_WEB=1 KISAK_SP=1 dNODEBUG=1
        KISAK_WEB_SKIP_FLOATZ_LIGHTING=$<BOOL:${KISAK_WEB_SKIP_FLOATZ_LIGHTING}>
        KISAK_WEB_BATCH_DYNAMIC_UPLOAD_ERRORS=$<BOOL:${KISAK_WEB_BATCH_DYNAMIC_UPLOAD_ERRORS}>
        # MSVC integral spellings occur in the bundled ODE sources before an
        # engine header can provide the usual compatibility macros.
        __int64=long\ long __int32=int __int16=short __int8=char
        KISAK_WEB_REUSE_WORLD_STATIC_STATE=$<BOOL:${KISAK_WEB_REUSE_WORLD_STATIC_STATE}>
        KISAK_WEB_SUPPRESS_AUDIO_EQUALITY=$<BOOL:${KISAK_WEB_SUPPRESS_AUDIO_EQUALITY}>
        KISAK_WEB_SKIP_UNUSED_SHADOW_BOUNDS=$<BOOL:${KISAK_WEB_SKIP_UNUSED_SHADOW_BOUNDS}>
        KISAK_WEB_BATCH_SHADOW_ERRORS=$<BOOL:${KISAK_WEB_BATCH_SHADOW_ERRORS}>
        KISAK_WEB_DIRECT_CLOUD_APPEND=$<BOOL:${KISAK_WEB_DIRECT_CLOUD_APPEND}>)
    target_compile_options(${TARGET_NAME} PRIVATE
        "-fdeclspec"
        "-sUSE_ZLIB=1"
        # Keep allocation recovery and canonical setjmp/longjmp inside Wasm.
        # JS exception trampolines cannot suspend through a JSPI loading yield.
        "-fwasm-exceptions"
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Debug>:-g3>"
        "$<$<NOT:$<CONFIG:Debug>>:-${KISAK_WEB_OPTIMIZATION}>"
        "$<$<BOOL:${KISAK_WEB_FULL_LTO}>:-flto>"
        "$<$<BOOL:${KISAK_WEB_SIMD}>:-msimd128>"
    )
endfunction()

function(kisak_configure_web_target TARGET_NAME)
    kisak_configure_web_compile_target(${TARGET_NAME})

    set(KISAK_WEB_EXPORTED_FUNCTIONS
        "_main,_malloc,_free,_KisakWeb_ProbeLocalization,_KisakWeb_ProbeIwd,_KisakWeb_ProbeFastfileHeader,_KisakWeb_MountCanonicalRuntime,_KisakWeb_SubmitCanonicalCommand,_KisakWeb_QueueKeyEvent,_KisakWeb_QueueCharEvent,_KisakWeb_QueueMouseMove,_KisakWeb_SetClipboardText")
    if (ARGV1 STREQUAL "DIAGNOSTICS")
        set(KISAK_WEB_EXPORTED_FUNCTIONS
            "${KISAK_WEB_EXPORTED_FUNCTIONS},_KisakWeb_CanonicalFsFileSize,_KisakWeb_CanonicalFsListCount,_KisakWeb_CanonicalFsReadHash,_KisakWeb_CanonicalFsWriteRename,_KisakWeb_DiagnosticCinematicOmission,_KisakWeb_TestAudioProxyPcm,_KisakWeb_TestLoseWebGLContext,_KisakWeb_TestRestoreWebGLContext,_KisakWeb_TestSetAaSamples,_KisakWeb_TestSubmitSurface,_KisakWeb_TestSlowNextCommand,_KisakWeb_TestUiState,_KisakWeb_TestMenuState,_KisakWeb_TestResumeGame,_KisakWeb_TestObjectiveNotification,_KisakWeb_TestUiTextSeen,_KisakWeb_TestConfigState,_KisakWeb_TestProfileState,_KisakWeb_TestSaveState")
    endif()

    target_link_options(${TARGET_NAME} PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:-${KISAK_WEB_OPTIMIZATION}>"
        "$<$<BOOL:${KISAK_WEB_SIMD}>:-msimd128>"
        "-sUSE_ZLIB=1"
        "-fwasm-exceptions"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=worker"
        "-sOFFSCREENCANVAS_SUPPORT=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        # Native Wasm suspension keeps loading synchronous-looking without
        # Asyncify instrumentation. The launcher requires JSPI support.
        "-sJSPI=1"
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
