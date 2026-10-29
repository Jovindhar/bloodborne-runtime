# DXC: only the command line compiler and its library, out of the HLSL fork of LLVM. Configured
# with its own cache preset, which keeps the build to what the compiler needs; the generated sources
# are copied rather than compared (the comparison fails with newer Pythons).
execute_process(COMMAND ${CMAKE_COMMAND}
        -S "${BB_SRC}" -B "${BB_BIN}" -G Ninja
        -C "${BB_SRC}/cmake/caches/PredefinedParams.cmake"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_ARCHITECTURES=${BB_ARCH}
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${BB_MIN_MACOS}
        -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF
        -DSPIRV_BUILD_TESTS=OFF -DLLVM_TARGETS_TO_BUILD=None -DHLSL_COPY_GENERATED_SOURCES=ON
    RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "dxc's configure failed (${rc})")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build "${BB_BIN}" --target dxc -j ${BB_CORES}
                RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "dxc's build failed (${rc})")
endif()
file(GLOB bb_dxc "${BB_BIN}/bin/dxc-*")
if (NOT bb_dxc)
    message(FATAL_ERROR "no bin/dxc-* after dxc's build")
endif()
file(MAKE_DIRECTORY "${BB_PREFIX}/bin" "${BB_PREFIX}/lib")
list(GET bb_dxc 0 bb_dxc_bin)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${bb_dxc_bin}" "${BB_PREFIX}/bin/dxc")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${BB_BIN}/lib/libdxcompiler.dylib"
                "${BB_PREFIX}/lib/libdxcompiler.dylib")
