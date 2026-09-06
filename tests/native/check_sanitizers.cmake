foreach(kind asan ubsan)
    execute_process(COMMAND "${EXECUTABLE}" "${kind}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    if(kind STREQUAL "asan")
        set(diagnostic "AddressSanitizer: heap-buffer-overflow")
    else()
        set(diagnostic "runtime error: signed integer overflow")
    endif()
    if(NOT "${result}" MATCHES "^[1-9][0-9]*$" OR
       NOT "${error}" MATCHES "KISAK_SANITIZER_CHILD_STARTED" OR
       NOT "${error}" MATCHES "${diagnostic}" OR
       "${error}" MATCHES "KISAK_SANITIZER_CHILD_RETURNED")
        message(FATAL_ERROR "${kind} did not stop its intended failure: ${result}\n${output}\n${error}")
    endif()
endforeach()
