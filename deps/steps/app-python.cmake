# Python for the app bundle: python-build-standalone, relocatable and stripped, of which only the
# runtime is kept (ditto keeps its symlinks and permissions).
if (EXISTS "${BB_SRC}/python")
    set(bb_python "${BB_SRC}/python")
else()
    set(bb_python "${BB_SRC}") # the stripped archive unpacks bin/, lib/ and share/ at the root
endif()
execute_process(COMMAND ditto "${bb_python}" "${BB_PREFIX}/app-runtime/python" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "copying Python failed (${rc})")
endif()
# What the start-up scripts never use: the test suites, the editors and the tooling.
file(GLOB bb_python_lib "${BB_PREFIX}/app-runtime/python/lib/python3."*)
foreach(dir IN LISTS bb_python_lib)
    file(REMOVE_RECURSE
        "${dir}/test" "${dir}/idlelib" "${dir}/tkinter" "${dir}/turtledemo" "${dir}/ensurepip"
        "${dir}/lib2to3" "${dir}/pydoc_data" "${dir}/unittest/test" "${dir}/sqlite3/test"
        "${dir}/ctypes/test" "${dir}/site-packages/pip")
    file(GLOB bb_tkinter "${dir}/lib-dynload/_tkinter"*)
    file(REMOVE ${bb_tkinter})
endforeach()
file(REMOVE_RECURSE "${BB_PREFIX}/app-runtime/python/include" "${BB_PREFIX}/app-runtime/python/share")
# and the commands that go with them.
file(GLOB bb_scripts "${BB_PREFIX}/app-runtime/python/bin/pip*"
                     "${BB_PREFIX}/app-runtime/python/bin/idle*"
                     "${BB_PREFIX}/app-runtime/python/bin/pydoc*")
file(REMOVE ${bb_scripts})
