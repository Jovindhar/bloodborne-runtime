# bash for the app bundle: only system libraries (no readline or gettext from Homebrew), 4.4 or
# newer is what run.sh and the start-up scripts need. The app runs it from Contents/MacOS.
if (BB_ARCH STREQUAL "arm64")
    set(bb_host aarch64-apple-darwin)
else()
    set(bb_host x86_64-apple-darwin)
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E env
        "CFLAGS=-arch ${BB_ARCH} -O2 -mmacosx-version-min=${BB_MIN_MACOS}"
        "${BB_SRC}/configure" --prefix=${BB_PREFIX}/app-runtime --host=${bb_host}
        --disable-nls --without-bash-malloc --disable-readline --enable-static-link=no
    WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "bash's configure failed (${rc})")
endif()
execute_process(COMMAND make -j ${BB_CORES} WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "bash's build failed (${rc})")
endif()
file(MAKE_DIRECTORY "${BB_PREFIX}/app-runtime/bin")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${BB_SRC}/bash" "${BB_PREFIX}/app-runtime/bin/bash")
