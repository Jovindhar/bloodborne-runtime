# Every library in the prefix becomes relocatable: its own id and the references to the other
# libraries here are @rpath paths, and the pkg-config files find the prefix next to themselves.
# The build that links them passes -rpath deps/macos-<arch>/lib, so moving the checkout does not
# need an hour of rebuilding. Runs after every install; files that are already right are left
# alone, so nothing is touched and nothing looks newer than the build that used it.
file(GLOB bb_dylibs "${BB_PREFIX}/lib/*.dylib")
foreach(lib IN LISTS bb_dylibs)
    if (IS_SYMLINK "${lib}")
        continue() # libSDL3.dylib -> libSDL3.0.dylib: the file behind it is the one with the id
    endif()
    get_filename_component(base "${lib}" NAME)

    # otool -D prints one header per architecture (MoltenVK ships both), so the ids are compared
    # line by line: the file is only touched when one of them is not the @rpath one.
    execute_process(COMMAND otool -D "${lib}" OUTPUT_VARIABLE ids ERROR_VARIABLE bb_err)
    string(REPLACE "\n" ";" ids "${ids}")
    set(outdated FALSE)
    foreach(id IN LISTS ids)
        string(STRIP "${id}" id)
        if (NOT id STREQUAL "" AND NOT id MATCHES ":$" AND NOT id STREQUAL "@rpath/${base}")
            set(outdated TRUE)
        endif()
    endforeach()
    if (outdated)
        execute_process(COMMAND install_name_tool -id "@rpath/${base}" "${lib}" RESULT_VARIABLE rc)
        if (rc)
            message(FATAL_ERROR "install_name_tool -id failed on ${lib} (${rc})")
        endif()
    endif()

    execute_process(COMMAND otool -L "${lib}" OUTPUT_VARIABLE refs ERROR_VARIABLE bb_err)
    string(REPLACE "\n" ";" lines "${refs}")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line) # otool indents its entries with a tab
        if (line MATCHES ":$")
            continue() # the library's own name, which otool prints first (once per architecture)
        endif()
        string(REGEX MATCH "^([^ ]+)" ref "${line}")
        set(dep "${CMAKE_MATCH_1}")
        if (dep MATCHES "^${BB_PREFIX}/lib/(.+)$")
            execute_process(COMMAND install_name_tool -change "${dep}" "@rpath/${CMAKE_MATCH_1}"
                                    "${lib}" RESULT_VARIABLE rc)
            if (rc)
                message(FATAL_ERROR "install_name_tool -change failed on ${lib} (${rc})")
            endif()
        endif()
    endforeach()
endforeach()

# prefix=/Users/someone/runtime/deps/macos-arm64 becomes prefix=${pcfiledir}/../.., which pkg-config
# resolves against wherever the file is now.
file(GLOB bb_pkgconfigs "${BB_PREFIX}/lib/pkgconfig/*.pc" "${BB_PREFIX}/share/pkgconfig/*.pc")
foreach(pc IN LISTS bb_pkgconfigs)
    file(READ "${pc}" text)
    string(REPLACE "prefix=${BB_PREFIX}" "prefix=\${pcfiledir}/../.." relocatable "${text}")
    if (NOT relocatable STREQUAL text)
        file(WRITE "${pc}" "${relocatable}")
    endif()
endforeach()
