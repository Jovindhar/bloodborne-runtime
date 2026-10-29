# Zydis, the x86 decoder bbcpu uses, with its Zycore submodule. Zycore has no install rules of its
# own, so it is installed into the prefix first and Zydis's package files find it there; the
# installed Zycore target can lack its include directory, which is passed to Zydis by hand.
set(bb_cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=${BB_ARCH}
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${BB_MIN_MACOS} -DCMAKE_INSTALL_PREFIX=${BB_PREFIX}
    -DCMAKE_PREFIX_PATH=${BB_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.10)

execute_process(COMMAND ${CMAKE_COMMAND} -S "${BB_SRC}/dependencies/zycore" -B "${BB_BIN}/zycore"
                        ${bb_cmake} RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (rc EQUAL 0)
    execute_process(COMMAND ${CMAKE_COMMAND} --build "${BB_BIN}/zycore" -j ${BB_CORES}
                    RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
endif()
if (rc EQUAL 0)
    execute_process(COMMAND ${CMAKE_COMMAND} --install "${BB_BIN}/zycore" RESULT_VARIABLE rc
                    COMMAND_ECHO STDOUT)
endif()
if (rc EQUAL 0)
    execute_process(COMMAND ${CMAKE_COMMAND} -S "${BB_SRC}" -B "${BB_BIN}/zydis" ${bb_cmake}
            -DZYAN_SYSTEM_ZYCORE=ON -DCMAKE_C_FLAGS=-I${BB_PREFIX}/include
            -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_DOXYGEN=OFF
        RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
endif()
if (rc EQUAL 0)
    execute_process(COMMAND ${CMAKE_COMMAND} --build "${BB_BIN}/zydis" -j ${BB_CORES}
                    RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
endif()
if (rc EQUAL 0)
    execute_process(COMMAND ${CMAKE_COMMAND} --install "${BB_BIN}/zydis" RESULT_VARIABLE rc
                    COMMAND_ECHO STDOUT)
endif()
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "zydis failed (${rc})")
endif()
