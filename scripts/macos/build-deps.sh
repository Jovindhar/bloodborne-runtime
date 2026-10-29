#!/usr/bin/env bash
# macOS ships bash 3.2; this script needs 4.4 or newer (brew install bash).
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    for candidate in /opt/homebrew/bin/bash /usr/local/bin/bash; do
        if [[ -x $candidate ]]; then exec "$candidate" "$0" "$@"; fi
    done
    echo 'bash 4.4 or newer is needed (macOS: brew install bash).' >&2; exit 1
fi
set -euo pipefail
# macOS: the libraries the game links against, built into deps/macos-<arch> by deps/CMakeLists.txt.
# deps/packages.cmake has every version and checksum; a package that is installed is not touched, so
# this is cheap to call before a build.
#   bash scripts/macos/build-deps.sh                  # arm64 (the app's architecture)
#   BB_ARCH=x86_64 bash scripts/macos/build-deps.sh   # the Rosetta 2 build
#   cmake --build build/deps-arm64 --target deps-clean # install everything again
# Build tools that only run on the host (pkg-config, glslang, nasm) come from Homebrew.
cd -- "$(dirname -- "$0")/../.."
arch=${BB_ARCH:-}
if [[ -z $arch ]]; then
    [[ $(sysctl -n hw.optional.arm64 2>/dev/null) == 1 ]] && arch=arm64 || arch=x86_64
fi
for tool in pkg-config cmake ninja glslangValidator; do
    if ! command -v $tool >/dev/null; then
        echo "Missing $tool: brew install pkgconf cmake ninja nasm glslang bash" >&2; exit 1
    fi
done
build=build/deps-$arch
cmake -S deps -B "$build" -G Ninja -DBB_ARCH="$arch" "$@"
cmake --build "$build"
echo "Dependencies are in $PWD/deps/macos-$arch"
