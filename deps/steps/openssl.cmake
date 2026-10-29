# OpenSSL: its own Configure (Perl), static libraries only, for the online module's TLS. install_sw
# leaves out the manual pages.
if (BB_ARCH STREQUAL "arm64")
    set(bb_ssl_target darwin64-arm64-cc)
else()
    set(bb_ssl_target darwin64-x86_64-cc)
endif()
execute_process(COMMAND perl "${BB_SRC}/Configure" ${bb_ssl_target} no-shared no-tests no-docs
    --prefix=${BB_PREFIX} --openssldir=${BB_PREFIX}/etc/ssl --libdir=lib
    -mmacosx-version-min=${BB_MIN_MACOS}
    WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "openssl's Configure failed (${rc})")
endif()
execute_process(COMMAND make -j ${BB_CORES} WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "openssl's build failed (${rc})")
endif()
execute_process(COMMAND make install_sw WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "openssl's install failed (${rc})")
endif()
