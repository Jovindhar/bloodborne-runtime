# The bundle carries only @-relative search paths: the ones the build tree needs (deps/lib, the GPU
# library's directory) are dead weight in a shipped app, and on the machine that built it they
# would hide a library that the bundle forgot to copy.
file(GLOB bb_installed "${CMAKE_INSTALL_PREFIX}/Contents/MacOS/*"
     "${CMAKE_INSTALL_PREFIX}/Contents/Frameworks/*.dylib")
foreach(binary IN LISTS bb_installed)
    execute_process(COMMAND otool -l "${binary}" OUTPUT_VARIABLE listing ERROR_VARIABLE bb_err)
    string(REPLACE "\n" ";" lines "${listing}")
    set(in_rpath FALSE)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if (line STREQUAL "cmd LC_RPATH")
            set(in_rpath TRUE)
        elseif (in_rpath AND line MATCHES "^path ([^ ]+)")
            set(rpath ${CMAKE_MATCH_1})
            set(in_rpath FALSE)
            if (NOT rpath MATCHES "^@")
                execute_process(COMMAND install_name_tool -delete_rpath "${rpath}" "${binary}"
                                RESULT_VARIABLE rc)
                if (rc)
                    message(FATAL_ERROR "install_name_tool -delete_rpath ${rpath} failed on "
                                        "${binary} (${rc})")
                endif()
            endif()
        endif()
    endforeach()
endforeach()
