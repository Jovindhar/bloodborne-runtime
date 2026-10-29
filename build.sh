#!/usr/bin/env bash
# macOS ships bash 3.2; this script needs 4.4 or newer (brew install bash).
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    for candidate in /opt/homebrew/bin/bash /usr/local/bin/bash; do
        if [[ -x $candidate ]]; then exec "$candidate" "$0" "$@"; fi
    done
    echo 'bash 4.4 or newer is needed (macOS: brew install bash).' >&2; exit 1
fi
set -euo pipefail
# The game. CMakeLists.txt and CMakePresets.json are the build: one preset per configuration
# (macos-arm64, macos-arm64-debug, macos-arm64-asan, macos-x86_64, linux), Ninja underneath, the
# binaries in out/.
#   bash build.sh                 # this platform's preset
#   bash build.sh --test          # and the tests (ctest)
#   BB_PRESET=macos-arm64-asan bash build.sh
#   BB_PGO=generate bash build.sh # the GPU library with profile counters (Linux; see pgo/)
# BB_ARCH picks the native (arm64, bbcpu translates the game's code) or the Rosetta 2 (x86_64)
# build; BB_LTO=OFF and BB_SANITIZE work as the cache variables do.
cd -- "$(dirname -- "$0")"
macos=0
if [[ $(uname -s) == Darwin ]]; then macos=1; fi
if [[ -z ${BB_ARCH:-} ]]; then
    if (( macos )); then
        [[ $(sysctl -n hw.optional.arm64 2>/dev/null) == 1 ]] && BB_ARCH=arm64 || BB_ARCH=x86_64
    else
        BB_ARCH=$(uname -m)
    fi
fi
# The tools first: building the dependencies is the long part, and it should not start on a machine
# that cannot finish the job. On NixOS shell.nix provides them, and the build re-enters it.
if ! { command -v pkg-config >/dev/null && command -v cmake >/dev/null &&
       command -v ninja >/dev/null; }; then
    if [[ -z ${BB_IN_NIX_SHELL:-} ]] && command -v nix-shell >/dev/null; then
        exec env BB_IN_NIX_SHELL=1 nix-shell shell.nix --run "bash build.sh $*"
    fi
    echo 'Need pkg-config, cmake and ninja (see shell.nix).' >&2; exit 1
fi
if (( macos )); then
    # The libraries the game links against, in deps/macos-<arch> (deps/packages.cmake holds the
    # versions and the checksums). A package that is installed is not touched.
    BB_ARCH=$BB_ARCH bash scripts/macos/build-deps.sh
    # The check below, and the build, have to see the SDL3 in deps/, not a Homebrew one: these are
    # the two directories CMakeLists.txt gives pkg-config, and the commands deps/ installs.
    deps=$PWD/deps/macos-$BB_ARCH
    export PKG_CONFIG_LIBDIR=$deps/lib/pkgconfig:$deps/share/pkgconfig
    export PATH=$deps/bin:$PATH
fi
# The libraries themselves: SDL3 (and Vulkan on Linux), from deps/ on macOS and from shell.nix on
# Linux.
pkg_dependencies=(sdl3)
if (( ! macos )); then pkg_dependencies=(vulkan sdl3); fi
if ! pkg-config --exists "${pkg_dependencies[@]}"; then
    echo "pkg-config does not find ${pkg_dependencies[*]}" >&2
    if (( macos )); then
        echo "The dependencies in $deps are incomplete: bash scripts/macos/build-deps.sh says why." >&2
    else
        echo 'On Linux they come from shell.nix.' >&2
    fi
    exit 1
fi
# Submodules (git clone --recursive, or: git submodule update --init). The port's changes to
# FSR-Vulkan (gpu/patches/) are applied to a copy in the GPU library's build directory
# (gpu/CMakeLists.txt), so the submodule keeps matching its commit.
if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ]]; then
    git submodule update --init --recursive
fi
# BB_PGO: generate (instrumented build that writes pgo/ while the game runs), use, off. Default:
# use the profile in pgo/ when there is one. The profiles are GCC's, so macOS builds without.
pgo=${BB_PGO:-}
if (( macos )); then pgo=off; fi
if [[ -z $pgo ]]; then
    if [[ -n $(find pgo -name '*.gcda' -print -quit 2>/dev/null) ]]; then pgo=use; else pgo=off; fi
fi
if (( macos )); then
    preset=${BB_PRESET:-macos-$BB_ARCH}
else
    preset=${BB_PRESET:-linux}
fi
configure=(-DBB_PGO="$pgo" -DBB_LTO="${BB_LTO:-ON}")
if [[ -n ${CC:-} ]]; then configure+=(-DCMAKE_C_COMPILER="$CC"); fi
if [[ -n ${CXX:-} ]]; then configure+=(-DCMAKE_CXX_COMPILER="$CXX"); fi
if [[ -n ${BB_SANITIZE:-} ]]; then configure+=(-DBB_SANITIZE="$BB_SANITIZE"); fi
if [[ -n ${DLSS_SDK_ROOT:-} ]]; then configure+=(-DBB_DLSS_SDK_ROOT="$DLSS_SDK_ROOT"); fi
cmake --preset "$preset" "${configure[@]}"
cmake --build --preset "$preset"
if [[ ${1:-} == --test ]]; then
    cmake --build --preset "$preset" --target bb-tests
fi
# out/ is where run.sh, the launcher and the test scripts look: the build's own files stay in
# build/<preset>/, and this is the copy of the configuration that was just built.
mkdir -p out/gpu
for f in bb-probe bb-gpu-capabilities; do
    cmp -s "build/$preset/$f" "out/$f" 2>/dev/null || install -m755 "build/$preset/$f" "out/$f"
done
gpu_lib=build/"$preset"/gpu/libbbgpu.dylib
if (( ! macos )); then gpu_lib=build/"$preset"/gpu/libbbgpu.so; fi
cmp -s "$gpu_lib" out/gpu/"${gpu_lib##*/}" 2>/dev/null || install -m755 "$gpu_lib" out/gpu/
# The online module (BB_NET; macOS), beside it: runtime_net.c looks there.
net_lib=build/"$preset"/gpu/libbbnet20.dylib
if [[ -f $net_lib ]]; then
    cmp -s "$net_lib" out/gpu/libbbnet20.dylib 2>/dev/null || install -m755 "$net_lib" out/gpu/
fi
echo "Built $PWD/out/bb-probe (from build/$preset)"
