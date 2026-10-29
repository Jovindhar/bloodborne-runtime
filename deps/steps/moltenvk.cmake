# MoltenVK: its release has one dynamic library per platform. The ICD manifest next to the loader's
# own files points at ../../../lib/libMoltenVK.dylib, which stays right when the prefix moves
# (run.sh hands it to the loader through VK_DRIVER_FILES).
file(GLOB_RECURSE bb_dylibs "${BB_SRC}/libMoltenVK.dylib")
list(FILTER bb_dylibs INCLUDE REGEX "/macOS/")
if (NOT bb_dylibs)
    message(FATAL_ERROR "no macOS libMoltenVK.dylib in the MoltenVK release")
endif()
list(GET bb_dylibs 0 bb_dylib)

execute_process(COMMAND lipo -info "${bb_dylib}" OUTPUT_VARIABLE bb_slices RESULT_VARIABLE rc)
if (NOT rc EQUAL 0 OR NOT bb_slices MATCHES "${BB_ARCH}")
    message(FATAL_ERROR "the MoltenVK release has no ${BB_ARCH} slice (${bb_slices})")
endif()

file(MAKE_DIRECTORY "${BB_PREFIX}/lib" "${BB_PREFIX}/share/vulkan/icd.d")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${bb_dylib}" "${BB_PREFIX}/lib/libMoltenVK.dylib")
file(WRITE "${BB_PREFIX}/share/vulkan/icd.d/MoltenVK_icd.json" "{
    \"file_format_version\": \"1.0.0\",
    \"ICD\": {
        \"library_path\": \"../../../lib/libMoltenVK.dylib\",
        \"api_version\": \"1.4.0\",
        \"is_portability_driver\": true
    }
}
")
