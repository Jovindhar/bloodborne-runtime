#!/usr/bin/env bash
# tools/fsr4_metal_initializers.sh <amd_fidelityfx_upscaler_dx12.dll> [OUT]
#
# FSR 4.1.1 on Metal (gpu/fsr4_metal) needs two files of AMD's model from the player's own
# upscaler DLL (OptiScaler ships it, many games do; nothing of AMD's is downloaded or shipped):
# initializers.bin and initializers-ultraperformance.bin, 131,072 bytes each, at fixed offsets of
# the FSR 4.1.1.2740 DLL (AMD FidelityFX SDK 2.3.0). The DLL and both files are checked by SHA-256.
# OUT: where they go (default: $BB_DATA_DIR/fsr4_411, else fsr4_411 in this tree), the folder
# run.sh passes to the game as BB_FSR411_DIR. Needs only dd and shasum (macOS has both).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
dll=${1:?usage: tools/fsr4_metal_initializers.sh <amd_fidelityfx_upscaler_dx12.dll> [OUT]}
out=${2:-${BB_DATA_DIR:-.}/fsr4_411}
dll_sha=d0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
if [[ $(sha "$dll") != "$dll_sha" ]]; then
    echo "Not the FSR 4.1.1.2740 upscaler DLL (amd_fidelityfx_upscaler_dx12.dll of FidelityFX SDK 2.3.0)." >&2
    exit 2
fi
mkdir -p "$out"
# name, offset (bytes), SHA-256 of the 131,072 bytes there
while read -r name offset expected; do
    dd if="$dll" of="$out/$name.part" bs=512 skip=$((offset / 512)) count=256 status=none
    if [[ $(sha "$out/$name.part") != "$expected" ]]; then
        rm -f -- "$out/$name.part"
        echo "$name: unexpected bytes in the DLL" >&2
        exit 3
    fi
    mv "$out/$name.part" "$out/$name"
done <<'EOF'
initializers.bin 12601856 a54e552ff69a3f7199861417f7ae461d1f67f636ab532c6ec9d8ea5d2963735a
initializers-ultraperformance.bin 12732928 3c42fc6eabfa9b5b1e489df0e9b2f58e3f9fa22048c584f6839bb27d32a0640e
EOF
echo "FSR 4.1.1 initializers in $out"
