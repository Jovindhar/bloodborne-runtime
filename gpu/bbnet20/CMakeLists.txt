# The online module: libbbnet20. shadPS4 v0.20.0's network, NP and shadNet sources
# (vendor/shadps4, 529ceb3, unchanged but for vendor-changes.diff) and The Hunter's Dream backend
# (thd/, from droogie/bbhost) (SHADNET-PORT.md). The pools: "shadnet" (Seamless Hunt) and
# "huntersdream". Hidden symbols: the module never meets libbbgpu's copies of shadPS4's code.

set(BBPORT_VERSION "0.6-pre1")
set(BBNET_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shadps4")
set(SHADNET_PROTO_OUT20 "${CMAKE_CURRENT_BINARY_DIR}/shadnet_proto_gen")
file(MAKE_DIRECTORY "${SHADNET_PROTO_OUT20}")
add_custom_command(
    OUTPUT "${SHADNET_PROTO_OUT20}/shadnet.pb.cc" "${SHADNET_PROTO_OUT20}/shadnet.pb.h"
    COMMAND protobuf::protoc "--proto_path=${BBNET_VENDOR}/shadnet"
            "--cpp_out=${SHADNET_PROTO_OUT20}" "${BBNET_VENDOR}/shadnet/shadnet.proto"
    DEPENDS "${BBNET_VENDOR}/shadnet/shadnet.proto")
file(GLOB_RECURSE BBNET20_SOURCES CONFIGURE_DEPENDS
    vendor/shadps4/core/libraries/net/*.cpp vendor/shadps4/core/libraries/network/*.cpp
    vendor/shadps4/core/libraries/np/*.cpp vendor/shadps4/core/net/*.cpp
    vendor/shadps4/shadnet/*.cpp)
# Trophies, commerce and the profile and Facebook dialogs stay in the C runtime.
list(FILTER BBNET20_SOURCES EXCLUDE REGEX
    "np_trophy|trophy_ui|np_sns_facebook_dialog|np_profile_dialog/|np_commerce/")
# BloodOnline: BBSC, Bloodborne seamless co-op (CLS_AMG, GPL-2.0+, from bbport 0.6): its code and
# the port's stand-ins for shadPS4's UI (bbsc/bbsc_glue.cpp). Installed only with BB_BBSC=1.
file(GLOB BBNET20_BBSC_SOURCES CONFIGURE_DEPENDS bbsc/core/*.cpp bbsc/bbsc_glue.cpp)
file(GLOB BBNET20_THD_SOURCES CONFIGURE_DEPENDS thd/*.cpp compat/common/*.cpp)

add_library(bbnet20 SHARED ${BBNET20_SOURCES} ${BBNET20_THD_SOURCES} ${BBNET20_BBSC_SOURCES}
    "${SHADNET_PROTO_OUT20}/shadnet.pb.cc" bbnet_glue.cpp)
target_include_directories(bbnet20 PRIVATE include bbsc compat . .. ../shim ../shadps4 "${BBNET_VENDOR}"
    "${BBNET_VENDOR}/shadnet" "${SHADNET_PROTO_OUT20}" ../third_party/imgui ../third_party/httplib)
# The version the shadNet client reports to the server (BB_CLIENT_VERSION overrides it).
target_compile_definitions(bbnet20 PRIVATE BOOST_ASIO_STANDALONE NDEBUG
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT BBNET_PORT_VERSION="${BBPORT_VERSION}")
target_compile_options(bbnet20 PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden
    -Wno-deprecated-declarations -Wno-missing-field-initializers)
if (APPLE)
    # Calls into the C runtime are resolved by bb-probe when the module is selected.
    # Only the module's interface is exported (its static OpenSSL, Protobuf and Abseil stay
    # inside), and the libraries libbbgpu links but this module never calls are dropped.
    target_link_options(bbnet20 PRIVATE -Wl,-undefined,dynamic_lookup "-Wl,-exported_symbol,_bbnet_*"
        -Wl,-dead_strip_dylibs)
    set_target_properties(bbnet20 PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    find_path(EPOLL_SHIM_INCLUDE sys/epoll.h PATH_SUFFIXES libepoll-shim)
    find_library(EPOLL_SHIM_LIBRARY epoll-shim)
    if (NOT EPOLL_SHIM_INCLUDE OR NOT EPOLL_SHIM_LIBRARY)
        message(FATAL_ERROR "bbnet20 on macOS needs epoll-shim")
    endif()
    target_include_directories(bbnet20 PRIVATE "${EPOLL_SHIM_INCLUDE}")
    target_link_libraries(bbnet20 PRIVATE "${EPOLL_SHIM_LIBRARY}")
else()
    target_link_options(bbnet20 PRIVATE -Wl,-Bsymbolic -Wl,--exclude-libs,ALL)
    set_target_properties(bbnet20 PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
# Beside libbbgpu (build/<preset>/gpu), where build.sh copies both from.
set_target_properties(bbnet20 PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(bbnet20 PRIVATE bbgpu nlohmann_json::nlohmann_json protobuf::libprotobuf
    OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB "${MINIUPNPC_LIBRARY}" Vulkan::Headers fmt::fmt
    Boost::headers magic_enum::magic_enum)
target_include_directories(bbnet20 PRIVATE "${MINIUPNPC_INCLUDE}")
target_link_directories(bbnet20 PRIVATE "${CMAKE_PREFIX_PATH}/lib")
