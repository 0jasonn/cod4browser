# GPL-3.0 synthetic fixture trace captured from the original Win32 db_load.cpp.
# The same expectation is checked for that oracle and the adapted Win32/Wasm
# loaders. Addresses are normalized by the fixture's publication observer.
if(NODE)
    set(command "${NODE}" "${EXECUTABLE}")
else()
    set(command "${EXECUTABLE}")
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE actual ERROR_VARIABLE errors)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Asset oracle failed (${result}):\n${actual}\n${errors}")
endif()
file(READ "${CMAKE_CURRENT_LIST_DIR}/db_asset_oracle_expected.txt" expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Asset oracle differs from original db_load.cpp trace:\n${actual}")
endif()
message(STATUS "Original db_load.cpp publication/block trace matches; rollback, lifetime and retry assertions passed")
