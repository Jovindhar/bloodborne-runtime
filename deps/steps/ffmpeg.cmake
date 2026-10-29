# FFmpeg: autoconf, and an in-source build (its configure writes the Makefiles there). The decoders
# the game's movies need (AvPlayer), shared; VideoToolbox for the hardware path.
if (BB_ARCH STREQUAL "arm64")
    set(bb_ffarch aarch64)
else()
    set(bb_ffarch x86_64)
endif()
execute_process(COMMAND "${BB_SRC}/configure"
    --prefix=${BB_PREFIX}
    --enable-cross-compile --arch=${bb_ffarch} --target-os=darwin
    "--cc=clang -arch ${BB_ARCH}" "--cxx=clang++ -arch ${BB_ARCH}" --x86asmexe=nasm
    --extra-cflags=-mmacosx-version-min=${BB_MIN_MACOS}
    "--extra-ldflags=-arch ${BB_ARCH} -mmacosx-version-min=${BB_MIN_MACOS}"
    --enable-shared --disable-static --disable-programs --disable-doc --disable-avdevice
    --disable-avfilter --disable-network --disable-autodetect --enable-videotoolbox
    WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "ffmpeg's configure failed (${rc})")
endif()
execute_process(COMMAND make -j ${BB_CORES} WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "ffmpeg's build failed (${rc})")
endif()
execute_process(COMMAND make install WORKING_DIRECTORY "${BB_SRC}" RESULT_VARIABLE rc
                COMMAND_ECHO STDOUT)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "ffmpeg's install failed (${rc})")
endif()
