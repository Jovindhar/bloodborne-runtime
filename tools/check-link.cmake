# The native Metal path decodes the game's commands and draws them itself: it resolves the Vulkan C
# entry points the shadPS4 video core calls (gpu/shadps4/video_core/renderer_metal/mtl_vk_c_bridge)
# and loads neither a Vulkan loader nor an ICD. These two checks are what keeps that true, and they
# run after linking, so a build that breaks it fails here instead of in a packaged app:
#
#   - the GPU library may not leave a _vk symbol unresolved (the bridge stopped answering it),
#   - and no binary of the game may link a Vulkan loader or MoltenVK.
#
# Both failures are quiet: the app starts and fails the first time it draws.

# The lines of a command's output that match an expression, one per line.
function(bb_matching_lines text expression out)
    string(REPLACE "\n" ";" lines "${text}")
    set(found "")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if (line MATCHES "${expression}")
            list(APPEND found "${line}")
        endif()
    endforeach()
    string(REPLACE ";" "\n  " found "${found}")
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

execute_process(COMMAND nm -u "${BB_GPU}" OUTPUT_VARIABLE undefined ERROR_VARIABLE bb_err)
bb_matching_lines("${undefined}" "_vk[A-Z]" unresolved)
if (unresolved)
    message(FATAL_ERROR "The GPU library leaves Vulkan entry points unresolved (the Metal bridge "
                        "does not answer them):\n  ${unresolved}")
endif()

foreach(binary IN LISTS BB_BINARIES)
    execute_process(COMMAND otool -L "${binary}" OUTPUT_VARIABLE libraries ERROR_VARIABLE bb_err)
    bb_matching_lines("${libraries}" "lib(vulkan|MoltenVK)" linked)
    if (linked)
        message(FATAL_ERROR "${binary} links a Vulkan loader or MoltenVK, and the native Metal path "
                            "loads neither:\n  ${linked}")
    endif()
endforeach()
