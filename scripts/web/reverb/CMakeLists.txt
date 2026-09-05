cmake_minimum_required(VERSION 3.24)
project(KisakReverb LANGUAGES C CXX)
include(FetchContent)
include(CTest)

# Same OpenAL Soft release as the native driver, pinned to its exact commit.
# Build separately: its headers, defines and device APIs must not leak into
# the game's deliberately narrow Web OpenAL proxy.
set(LIBTYPE STATIC)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_EAX OFF CACHE BOOL "" FORCE)
set(ALSOFT_EMBED_HRTF_DATA OFF CACHE BOOL "" FORCE)
if (EMSCRIPTEN)
    # Preserve the x86 reference's fastf2u round-to-nearest behavior for EAX
    # modulation. The generic scalar fallback truncates, changing its LFO.
    # Emscripten's supported SSE2 compatibility path keeps this upstream code.
    string(APPEND CMAKE_C_FLAGS " -msimd128 -msse2")
    string(APPEND CMAKE_CXX_FLAGS " -msimd128 -msse2")
    set(ALSOFT_REQUIRE_SSE2 ON CACHE BOOL "" FORCE)
    set(ALSOFT_CPUEXT_SSE3 OFF CACHE BOOL "" FORCE)
    set(ALSOFT_CPUEXT_SSE4_1 OFF CACHE BOOL "" FORCE)
    set(ALSOFT_CPUEXT_NEON OFF CACHE BOOL "" FORCE)
    # Clang's function-effects analysis incorrectly treats the consteval
    # _uz literal's checked 64->32 conversion as a possible runtime call.
    # Keep the source's checked conversions and STL/runtime assertions.
    set(HAVE_WFUNCTION_EFFECTS OFF CACHE BOOL "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-Oz -flto -DNDEBUG")
endif()
FetchContent_Declare(openal
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG b2c48f7718ef3fcf67921a8b6534c4914e328970)
FetchContent_MakeAvailable(openal)

get_filename_component(KISAK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if (EMSCRIPTEN)
    get_target_property(openal_sources OpenAL SOURCES)
    list(LENGTH openal_sources before_fpu)
    list(FILTER openal_sources EXCLUDE REGEX "(^|/)core/fpu_ctrl[.]cpp$")
    list(LENGTH openal_sources after_fpu)
    math(EXPR expected_fpu "${after_fpu} + 1")
    if (NOT before_fpu EQUAL expected_fpu)
        message(FATAL_ERROR "OpenAL FPU source inventory changed; review the platform adapter")
    endif()
    # This component calls the effect directly with OpenAL's default Mix_C.
    # It never initializes OpenAL's CPU-dispatched source mixer. That SSE1
    # implementation imports unsupported MMX intrinsics and is not linked.
    list(FILTER openal_sources EXCLUDE REGEX "(^|/)core/mixer/mixer_sse[.]cpp$")
    list(LENGTH openal_sources before_logging)
    list(FILTER openal_sources EXCLUDE REGEX "(^|/)core/logging[.]cpp$")
    list(LENGTH openal_sources after_logging)
    math(EXPR expected_logging "${after_logging} + 1")
    if (NOT before_logging EQUAL expected_logging)
        message(FATAL_ERROR "OpenAL logging inventory changed; review the worklet adapter")
    endif()
    set_property(TARGET OpenAL PROPERTY SOURCES "${openal_sources}")
    target_sources(OpenAL PRIVATE "${KISAK_ROOT}/src/web/web_audio_fpu.cpp"
        "${KISAK_ROOT}/src/web/web_audio_log.cpp")
endif()
add_library(kisak_reverb OBJECT "${KISAK_ROOT}/src/web/web_reverb.cpp")
target_compile_features(kisak_reverb PRIVATE cxx_std_20)
target_include_directories(kisak_reverb PRIVATE "${KISAK_ROOT}/src"
    "${openal_SOURCE_DIR}" "${openal_SOURCE_DIR}/common"
    "${openal_SOURCE_DIR}/gsl/include" "${openal_BINARY_DIR}")
target_link_libraries(kisak_reverb PUBLIC OpenAL::OpenAL)

add_executable(web_reverb_tests "${KISAK_ROOT}/tests/native/web_reverb_tests.cpp")
target_compile_features(web_reverb_tests PRIVATE cxx_std_20)
target_include_directories(web_reverb_tests PRIVATE "${KISAK_ROOT}/src")
target_link_libraries(web_reverb_tests PRIVATE kisak_reverb)
if (MSVC)
    target_compile_options(web_reverb_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(web_reverb_tests PRIVATE -UNDEBUG)
endif()
if (EMSCRIPTEN)
    add_executable(reverb_dsp $<TARGET_OBJECTS:kisak_reverb>)
    target_link_libraries(reverb_dsp PRIVATE OpenAL::OpenAL)
    target_link_options(reverb_dsp PRIVATE -Oz -flto --no-entry
        "-Wl,-Map=${CMAKE_BINARY_DIR}/reverb-dsp.map"
        "-sENVIRONMENT=worklet" "-sMODULARIZE=1" "-sEXPORT_ES6=1"
        "-sSINGLE_FILE=1" "-sWASM_ASYNC_COMPILATION=0" "-sFILESYSTEM=0"
        "-sINITIAL_MEMORY=33554432"
        "-sEXPORTED_FUNCTIONS=['_WebReverb_Initialize','_WebReverb_SetRoom','_WebReverb_Input','_WebReverb_Output','_WebReverb_Process','_WebReverb_Shutdown']"
        "-sEXPORTED_RUNTIME_METHODS=['HEAPF32']")
    set_target_properties(reverb_dsp PROPERTIES SUFFIX ".mjs")
    configure_file("${openal_SOURCE_DIR}/COPYING" "${CMAKE_BINARY_DIR}/reverb-license.txt" COPYONLY)
    target_link_options(web_reverb_tests PRIVATE -Oz -flto
        "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1" "-sNODERAWFS=1"
        "-sINITIAL_MEMORY=33554432")
    set_target_properties(web_reverb_tests PROPERTIES SUFFIX ".cjs")
    find_program(KISAK_NODE NAMES node REQUIRED)
    add_test(NAME web_reverb_tests COMMAND "${KISAK_NODE}" "$<TARGET_FILE:web_reverb_tests>")
else()
    add_test(NAME web_reverb_tests COMMAND web_reverb_tests)
endif()
