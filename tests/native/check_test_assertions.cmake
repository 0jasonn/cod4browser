execute_process(COMMAND ${NODE} "${EXECUTABLE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    TIMEOUT 15)
if (NOT "${result}" MATCHES "^[1-9][0-9]*$" OR
    NOT "${error}" MATCHES "KISAK_ASSERTION_CHILD_STARTED" OR
    NOT "${error}" MATCHES "KISAK_INTENTIONAL_ASSERTION_FAILURE")
    message(FATAL_ERROR "Assertion child did not fail its check: ${result}\n${output}\n${error}")
endif()
