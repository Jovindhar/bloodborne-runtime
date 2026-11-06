# The Yharnam Together online module: libytcoop. Bloodborne seamless co-op by Kaniel, AMANISES and
# Supermedo (github.com/Amanises-Yharnam-Together/BBSC-Yharnam-Together, GPL-2.0-or-later), its
# Windows port's network, PSN and co-op code (vendor/, YTCOOP-PORT.md) on this runtime. The runtime
# loads it instead of libbbnet20 when the launcher's online mode is Yharnam Together
# (BB_NET_MODULE). Only bbnet_* (bbnet.h) and bbcoop_* (vendor/bbport/gpu/bbcoop.h) are exported.

set(YT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vendor" CACHE PATH "Yharnam Together's sources")
set(YT_EMU "${YT_SRC}/emulator/src")
set(YT_PORT "${YT_SRC}/bbport/gpu")
set(YT_SERVER "${YT_SRC}/server")
# The release whose players this module joins: Hello's "version" (coop/build_fingerprint.h). A host
# on another release refuses the game; keep it equal to the vendored release (YTCOOP-PORT.md).
set(YT_RELEASE "1.4.0")

set(YT_PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/ytcoop_proto_gen")
file(MAKE_DIRECTORY "${YT_PROTO_OUT}")
add_custom_command(
    OUTPUT "${YT_PROTO_OUT}/shadnet.pb.cc" "${YT_PROTO_OUT}/shadnet.pb.h"
    COMMAND protobuf::protoc "--proto_path=${YT_SERVER}/src" "--cpp_out=${YT_PROTO_OUT}"
            "${YT_SERVER}/src/shadnet.proto"
    DEPENDS "${YT_SERVER}/src/shadnet.proto")
add_custom_command(
    OUTPUT "${YT_PROTO_OUT}/coop.pb.cc" "${YT_PROTO_OUT}/coop.pb.h"
    COMMAND protobuf::protoc "--proto_path=${YT_SERVER}/coop" "--cpp_out=${YT_PROTO_OUT}"
            "${YT_SERVER}/coop/coop.proto"
    DEPENDS "${YT_SERVER}/coop/coop.proto")

file(GLOB_RECURSE YT_NET_SOURCES CONFIGURE_DEPENDS ${YT_EMU}/core/libraries/network/*.cpp
    ${YT_EMU}/core/libraries/np/*.cpp ${YT_EMU}/shadnet/*.cpp)
list(FILTER YT_NET_SOURCES EXCLUDE REGEX
    "np_trophy|trophy_ui|np_sns_facebook_dialog|np_profile_dialog/|np_commerce/")
# The co-op code, as the Windows port builds it (its gpu/CMakeLists.txt), but for the guest code
# hooks: this runtime's (ytcoop_hooks.cpp, through libbbgpu) replace its bbcoop/guest_hooks.cpp.
file(GLOB YT_PORT_COOP CONFIGURE_DEPENDS ${YT_PORT}/bbcoop/*.cpp)
list(FILTER YT_PORT_COOP EXCLUDE REGEX "guest_hooks\\.cpp$")
file(GLOB YT_GAME_COOP CONFIGURE_DEPENDS ${YT_EMU}/core/bloodborne_*.cpp)
set(YT_SOURCES ${YT_NET_SOURCES} ${YT_PORT_COOP} ${YT_GAME_COOP}
    ${YT_SERVER}/coop/world_log.cpp ${YT_SERVER}/coop/coop_messages.cpp ${YT_SERVER}/coop/trade.cpp
    ${YT_SERVER}/coop/link_crypto.cpp ${YT_SERVER}/coop/duel.cpp
    ${YT_EMU}/core/file_format/psf.cpp
    # What the Windows port's libbbgpu gave these: shadPS4's common code and its logging.
    ${YT_PORT}/shadps4/common/path_util.cpp ${YT_PORT}/shadps4/common/string_util.cpp
    ${YT_PORT}/shadps4/common/io_file.cpp ${YT_PORT}/shadps4/common/thread.cpp
    ${YT_PORT}/shadps4/common/error.cpp ${YT_PORT}/shadps4/common/slot_vector.cpp
    ${YT_PORT}/shadps4/common/decoder.cpp
    ${YT_PORT}/shim/common/logging/log.cpp)

# The build's revision for the log (the Windows port's bbcoop_revision.h).
file(WRITE "${YT_PROTO_OUT}/bbcoop_revision.h.in"
    "#pragma once\n#define BB_SCM_REV \"bloodonline-ytcoop-${YT_RELEASE}\"\n")
configure_file("${YT_PROTO_OUT}/bbcoop_revision.h.in" "${YT_PROTO_OUT}/bbcoop_revision.h" COPYONLY)

add_library(ytcoop SHARED ${YT_SOURCES} "${YT_PROTO_OUT}/shadnet.pb.cc" "${YT_PROTO_OUT}/coop.pb.cc"
    ytcoop_glue.cpp ytcoop_hooks.cpp)
# The Windows port's include order: its shim and shadPS4 copy first, the emulator's sources last.
target_include_directories(ytcoop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" .. "${YT_PORT}/shim"
    "${YT_PORT}/shadps4" "${YT_PORT}" "${YT_PROTO_OUT}" "${YT_PORT}/third_party/imgui"
    "${YT_PORT}/third_party/httplib" "${YT_EMU}" "${YT_SERVER}")
target_compile_features(ytcoop PRIVATE cxx_std_23)
target_compile_definitions(ytcoop PRIVATE BOOST_ASIO_STANDALONE NDEBUG BB_PRODUCTION=1
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT
    BLOODBORNE_PARTY_DEATH_ALWAYS=1 BLOODBORNE_NATIVE_PORT=1 YTCOOP_RELEASE="${YT_RELEASE}")
target_compile_options(ytcoop PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/ytcoop_export.h"
    -Wno-deprecated-declarations -Wno-missing-field-initializers)
if (APPLE)
    # Calls into the C runtime and libbbgpu's hook installer are resolved in the process.
    target_link_options(ytcoop PRIVATE -Wl,-undefined,dynamic_lookup "-Wl,-exported_symbol,_bbnet_*"
        "-Wl,-exported_symbol,_bbcoop_*" -Wl,-dead_strip_dylibs)
    set_target_properties(ytcoop PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    find_path(EPOLL_SHIM_INCLUDE sys/epoll.h PATH_SUFFIXES libepoll-shim)
    find_library(EPOLL_SHIM_LIBRARY epoll-shim)
    if (NOT EPOLL_SHIM_INCLUDE OR NOT EPOLL_SHIM_LIBRARY)
        message(FATAL_ERROR "ytcoop on macOS needs epoll-shim")
    endif()
    target_include_directories(ytcoop PRIVATE "${EPOLL_SHIM_INCLUDE}")
    target_link_libraries(ytcoop PRIVATE "${EPOLL_SHIM_LIBRARY}")
else()
    target_link_options(ytcoop PRIVATE -Wl,-Bsymbolic -Wl,--exclude-libs,ALL)
    set_target_properties(ytcoop PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
set_target_properties(ytcoop PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
# The co-op code disassembles the game's instructions it reports (Zydis, as libbbgpu links it).
target_link_libraries(ytcoop PRIVATE Zydis::Zydis)
if (APPLE)
    target_link_directories(ytcoop PRIVATE "${CMAKE_PREFIX_PATH}/lib")
endif()
target_link_libraries(ytcoop PRIVATE nlohmann_json::nlohmann_json protobuf::libprotobuf
    OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB "${MINIUPNPC_LIBRARY}" fmt::fmt Boost::headers
    magic_enum::magic_enum)
target_include_directories(ytcoop PRIVATE "${MINIUPNPC_INCLUDE}")
