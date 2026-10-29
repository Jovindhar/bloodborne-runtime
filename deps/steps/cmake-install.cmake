# A CMake project: install what its build made into the prefix, then record it.
execute_process(COMMAND ${CMAKE_COMMAND} --install "${BB_BIN}" --prefix "${BB_PREFIX}"
                RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "cmake --install ${BB_BIN} failed (${rc})")
endif()
