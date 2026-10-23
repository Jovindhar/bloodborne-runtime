#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds what the Metal FSR 4.1.1 path loads (vk_fsr4_metal.mm), at build time, from
fsr4-native-macos (VENDOR.txt; scripts/macos/build-deps.sh puts its tree, DXC and the
SPIRV-Cross and SPIRV-Tools commands in deps/):

- fsr411_helpers.metallib, fsr411_simd.metallib (M1-M4) and fsr411_mpp_<tier>.metallib
  (M5, when this Xcode has Metal 4);
- <variant>/bindings.json and its metallibs: the PRE/POST/autoexposure/RCAS graph of each
  variant the game uses (compile_fsr411.py);
- fsr411_native_<tier>[_ultraperformance]_lossy/: the native network's manifest, parameters and
  recipe. AMD's initializers are never read here: the network's weights are bytes of the
  player's initializer, and the recipe says which (or a constant from the MIT HLSL import) for
  each weight byte. The Lossy folding and the simd Winograd weights are computed from those
  values, so the runtime does them after assembling (fsr411_runtime.h, assemble_weights), on
  the player's Mac, after checking the initializer's SHA-256.

--verify DIR (with AMD's initializers.bin and initializers-ultraperformance.bin in DIR):
checks that the recipe, folding and Winograd give the upstream generator's exact weights, and
that the same passes use Winograd.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import types

INIT_BYTES = 131072
GATHER = 1 << 31
# The variants the game asks for: depth 0 at the near plane, motion vectors at the render
# size and not jittered, linear colour, Lossy (vk_fsr4_metal.mm).
VARIANT = dict(depth_inverted=0, low_res_mv=1, jittered_mv=0, colorspace=0, optimize=1)


def run(command, cwd=None):
    result = subprocess.run([str(c) for c in command], cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f"FSR 4.1.1 assets: {' '.join(map(str, command))} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def variant_key(tier, ae, up):
    v = VARIANT
    return (f"fsr411_{tier}_di{v['depth_inverted']}_lm{v['low_res_mv']}_jm{v['jittered_mv']}"
            f"_cs{v['colorspace']}_ae{ae}_o{v['optimize']}" + ("_up1" if up else "") + "_lossy_pc1_ca1")


def native_dir(tier, up):
    return f"fsr411_native_{tier}" + ("_ultraperformance" if up else "") + "_lossy"


def metal_has_v4():
    probe = subprocess.run(["xcrun", "-sdk", "macosx", "metal", "-std=metal4.0", "-x", "metal",
                            "-c", "-", "-o", os.devnull], input="kernel void k(){}\n",
                           capture_output=True, text=True)
    return probe.returncode == 0


def build_kernels(here, out, tiers):
    air = out / ".air"
    air.mkdir(parents=True, exist_ok=True)

    def metallib(source, name, std, *defines):
        target = air / (name + ".air")
        run(["xcrun", "-sdk", "macosx", "metal", "-std=" + std, "-I", here, *defines, "-c",
             here / source, "-o", target])
        run(["xcrun", "-sdk", "macosx", "metallib", target, "-o", out / (name + ".metallib")])

    metallib("fsr411_helpers.metal", "fsr411_helpers", "macos-metal2.3")
    metallib("fsr411_simd.metal", "fsr411_simd", "metal3.0", "-DFSR411_SPECIALIZE=1")
    if metal_has_v4():
        for tier in tiers:
            metallib("fsr411_mpp.metal", f"fsr411_mpp_{tier}", "metal4.0", "-DFSR411_SPECIALIZE=1",
                     f"-DFSR411_MAXW={tier * 8 // 9}", f"-DFSR411_MAXH={tier // 2}")
    else:
        print("FSR 4.1.1 assets: this Xcode has no Metal 4; M5 Macs use the simd kernels")
    shutil.rmtree(air)


def build_bindings(src, out, work, tiers):
    """compile_fsr411.py builds in `work` (its cache: unchanged inputs are not rebuilt); `out`
    gets what the runtime loads: bindings.json, the metallibs and the literal tables."""
    for tier in tiers:
        for ae in (0, 1):
            for up in (0, 1):
                v = VARIANT
                key = variant_key(tier, ae, up)
                run([sys.executable, src / "tools/compile_fsr411.py", "--root", src, "--builddir", work,
                     "--res", tier, "--depth-inverted", v["depth_inverted"], "--low-res-mv",
                     v["low_res_mv"], "--jittered-mv", v["jittered_mv"], "--colorspace",
                     v["colorspace"], "--auto-exposure", ae, "--optimize", v["optimize"],
                     "--ultra-performance", up, "--profile", "lossy", "--external-post-cnb", 1,
                     "--strict-math", 0, "--post-cnb-alias", 1])
                bindings = work / key / "bindings.json"
                if not bindings.is_file():
                    sys.exit(f"FSR 4.1.1 assets: no bindings for {key}")
                kernels = json.loads(bindings.read_text())["kernels"].values()
                files = {"bindings.json", *(k["library"] for k in kernels),
                         *(k["literal_tables"] for k in kernels if k.get("literal_tables"))}
                target = out / key
                if target.exists():
                    shutil.rmtree(target)
                target.mkdir(parents=True)
                for name in files:
                    shutil.copy2(work / key / name, target / name)


# Range checks on values computed from the initializer (and its hash check): AMD's initializer
# passes them (checked again by --verify, and the runtime accepts only that initializer), the
# synthetic ones need not.
VALUE_CHECKS = ("POST accumulator exceeds", "float tail cannot be replaced", "integer requant may overflow",
                "downsample sum exceeds", "FNB/CT tail exceeds", "FNB integer accumulator",
                "INT8 dot products exceed", "expected the verified 131072-byte")
# The simd Winograd weights are kept for every pass the generator can transform: their exactness
# bounds depend on the values, so the runtime decides (assemble_weights).
WINOGRAD_BOUNDS = ("        if max(abs(v) for v in values) > 2048:\n            return None\n",
                   "        if worst >= 1 << 24:\n            return None\n")


def load_generator(src, synthetic, force_winograd=False):
    sys.path.insert(0, str(src / "tools"))
    path = src / "tools/gen_fsr411_native.py"
    text = path.read_text()
    if synthetic:
        for check in VALUE_CHECKS:
            text, count = re.subn(r'raise ValueError\("' + re.escape(check) + r'[^\n]*', "pass", text)
            if count != 1:
                sys.exit(f"FSR 4.1.1 assets: the generator changed (check '{check}')")
    if synthetic or force_winograd:
        for bound in WINOGRAD_BOUNDS:
            if text.count(bound) != 1:
                sys.exit("FSR 4.1.1 assets: the generator changed (Winograd bounds)")
            text = text.replace(bound, "")
    gen = types.ModuleType("bb_gen_fsr411_native" + ("_synthetic" if synthetic else "")
                           + ("_winograd" if force_winograd else ""))
    gen.__file__ = str(path)
    exec(compile(text, str(path), "exec"), gen.__dict__)
    return gen


def encode(gen, src, tier, up, initializer, fold):
    """The upstream generator's output for this initializer: (manifest, weights, params). With
    fold=False its Lossy folding is left to the runtime (it changes values, not positions)."""
    real_importlib = gen.importlib
    if not fold:
        def spec_from_file_location(name, location):
            loader = types.SimpleNamespace(exec_module=lambda m: setattr(
                m, "fold_conv3x3", lambda matrix, words: (list(matrix), {"drop_words_per_channel": words})))
            return types.SimpleNamespace(name=name, loader=loader)
        gen.importlib = types.SimpleNamespace(util=types.SimpleNamespace(
            spec_from_file_location=spec_from_file_location, module_from_spec=lambda s: types.ModuleType(s.name)))
    try:
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            init_path = temp / "initializer.bin"
            init_path.write_bytes(initializer)
            shaders = src / "third_party/fsr411/shaders"
            gen.encode_model(shaders, init_path, tier, temp / "out", "ultraperformance" if up else "normal",
                             shaders.parent / "ultraperformance", "lossy")
            out = temp / "out"
            return (json.loads((out / "fsr411_native_manifest.json").read_text()),
                    (out / "fsr411_native_weights.bin").read_bytes(),
                    (out / "fsr411_native_params.bin").read_bytes())
    finally:
        gen.importlib = real_importlib


def winograd_regions(manifest):
    """(entry, first byte, end) of each pass's simd Winograd weights (int16 values)."""
    for entry in [*manifest["passes"].values(), *manifest.get("auxiliary_passes", {}).values()]:
        w = entry.get("simd", {}).get("winograd")
        if w:
            yield entry, w["weights0"], w["weights0"] + 2 * w["weights0_count"]


def build_native(gen, src, out, tier, up):
    """Runs the generator on synthetic initializers: bytes that follow the initializer are
    gathered from it, the rest are constants. Five runs spell each gathered offset in nibbles.
    The Winograd regions are left to the runtime (computed from the folded weights)."""
    runs = {name: encode(gen, src, tier, up, data, fold=False) for name, data in (
        ("a", bytes([3]) * INIT_BYTES), ("b", bytes([9]) * INIT_BYTES),
        *((f"n{k}", bytes((i >> (4 * k)) & 15 for i in range(INIT_BYTES))) for k in range(5)))}
    manifest, weights_a, params = runs["a"]
    weights_b = runs["b"][1]
    for name, (other_manifest, weights, other) in runs.items():
        if other != params or len(weights) != len(weights_a) or \
                [r[1:] for r in winograd_regions(other_manifest)] != [r[1:] for r in winograd_regions(manifest)]:
            sys.exit(f"FSR 4.1.1 assets: the native layout depends on the initializer ({name})")
    computed = set()
    for _, first, end in winograd_regions(manifest):
        computed.update(range(first, end))
    recipe = []
    for i, (a, b) in enumerate(zip(weights_a, weights_b)):
        if i in computed or a == b:
            recipe.append(0 if i in computed else a)
            continue
        if (a, b) != (3, 9):
            sys.exit(f"FSR 4.1.1 assets: weight byte {i} is not a copy of an initializer byte")
        offset = sum(runs[f"n{k}"][1][i] << (4 * k) for k in range(5))
        if offset >= INIT_BYTES:
            sys.exit(f"FSR 4.1.1 assets: weight byte {i} reads past the initializer")
        recipe.append(GATHER | offset)
    # Only what the runtime reads, nothing computed from the synthetic values.
    for entry in [*manifest["passes"].values(), *manifest.get("auxiliary_passes", {}).values()]:
        entry.pop("accumulator_bounds", None)
        entry.get("simd", {}).pop("float_accumulator_bound", None)
        entry.get("simd", {}).get("winograd", {}).pop("float_accumulator_bound", None)
        folding = entry.get("lossy_transform", {}).get("conv3x3_folding")
        if folding:
            entry["lossy_transform"]["conv3x3_folding"] = {
                "drop_words_per_channel": folding["drop_words_per_channel"]}
    manifest.pop("weights_sha256", None)
    manifest["weights_recipe"] = "fsr411_native_recipe.bin"
    target = out / native_dir(tier, up)
    target.mkdir(parents=True, exist_ok=True)
    (target / "fsr411_native_params.bin").write_bytes(params)
    (target / "fsr411_native_recipe.bin").write_bytes(struct.pack(f"<{len(recipe)}I", *recipe))
    (target / "fsr411_native_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest, recipe


def winograd(conv):
    """The runtime's Winograd (assemble_weights): the generator's transform and bounds."""
    g2 = ((2, 0, 0), (1, 1, 1), (1, -1, 1), (0, 0, 2))
    at = ((1, 1, 1, 0), (0, 1, -1, -1))
    values = [sum(g2[xi][ky] * g2[nu][kx] * conv[(ky * 3 + kx) * 256 + ic * 16 + oc]
                  for ky in range(3) for kx in range(3))
              for xi in range(4) for nu in range(4) for ic in range(16) for oc in range(16)]
    if max(abs(v) for v in values) > 2048:
        return None
    worst = 0
    for oc in range(16):
        column = [512 * sum(abs(values[(p * 16 + ic) * 16 + oc]) for ic in range(16)) for p in range(16)]
        for i in range(2):
            for j in range(2):
                worst = max(worst, sum(column[xi * 4 + nu] for xi in range(4) for nu in range(4)
                                       if at[i][xi] and at[j][nu]))
    return None if worst >= 1 << 24 else values


def assemble(src, out, tier, up, initializer):
    """What the runtime's assemble_weights does, in Python: (weights, passes using Winograd)."""
    manifest = json.loads((out / native_dir(tier, up) / "fsr411_native_manifest.json").read_text())
    recipe = struct.unpack_from(f"<{manifest['weights_bytes']}I",
                                (out / native_dir(tier, up) / "fsr411_native_recipe.bin").read_bytes())
    weights = bytearray(initializer[r & ~GATHER] if r & GATHER else r for r in recipe)
    lossy = importlib.util.spec_from_file_location("bb_fsr411_lossy", src / "tools/fsr411/lossy.py")
    module = importlib.util.module_from_spec(lossy)
    lossy.loader.exec_module(module)
    for entry in manifest["passes"].values():
        folding = entry.get("lossy_transform", {}).get("conv3x3_folding")
        if folding:
            start = entry["weights"][0]
            matrix = [v - 256 if v > 127 else v for v in weights[start:start + 2304]]
            folded, _ = module.fold_conv3x3(matrix, folding["drop_words_per_channel"])
            weights[start:start + 2304] = bytes(v & 255 for v in folded)
    accepted = set()
    for entry, first, end in winograd_regions(manifest):
        start = entry["weights"][0]
        values = winograd([v - 256 if v > 127 else v for v in weights[start:start + 2304]])
        if values:
            weights[first:end] = struct.pack(f"<{len(values)}h", *values)
            accepted.add(entry.get("logical_pass", 13))
    return bytes(weights), accepted, manifest


def verify(gen, gen_forced, src, out, tier, up, init_dir):
    """The runtime's assembly against the upstream generator: the weights (the generator with its
    Winograd bounds lifted, which keeps the build's layout) and which passes use Winograd (the
    generator as it is)."""
    name = "initializers-ultraperformance.bin" if up else "initializers.bin"
    initializer = (init_dir / name).read_bytes()
    _, expected, _ = encode(gen_forced, src, tier, up, initializer, fold=True)
    real_manifest, _, _ = encode(gen, src, tier, up, initializer, fold=True)
    weights, accepted, manifest = assemble(src, out, tier, up, initializer)
    decided = {e.get("logical_pass", 13) for e, _, _ in winograd_regions(real_manifest)}
    skip = set()
    for entry, first, end in winograd_regions(manifest):
        if entry.get("logical_pass", 13) not in accepted:
            skip.update(range(first, end))
    same = len(weights) == len(expected) and all(a == b for i, (a, b) in enumerate(zip(weights, expected)) if i not in skip)
    if not same or accepted != decided:
        sys.exit(f"FSR 4.1.1 assets: {native_dir(tier, up)}: the runtime's weights differ from the generator's "
                 f"(Winograd passes {sorted(accepted)} vs {sorted(decided)})")
    print(f"FSR 4.1.1 assets: {native_dir(tier, up)}: the generator's exact weights; "
          f"Winograd in passes {sorted(accepted)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", type=Path, required=True, help="fsr4-native-macos tree (deps/share)")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--work", type=Path, help="compile_fsr411.py's build folder (default: OUT/../fsr4_metal_work)")
    parser.add_argument("--tiers", type=int, nargs="+", default=[1080, 2160])
    parser.add_argument("--verify", type=Path, help="folder with AMD's initializers (local check)")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    src, out = args.src.resolve(), args.out.resolve()
    if not args.verify and out.exists():
        shutil.rmtree(out)  # only this script's output: nothing stale from an earlier upstream
    out.mkdir(parents=True, exist_ok=True)
    gen = load_generator(src, synthetic=not args.verify)
    gen_forced = load_generator(src, synthetic=False, force_winograd=True) if args.verify else None
    if not args.verify:
        build_kernels(here, out, args.tiers)
        build_bindings(src, out, (args.work or out.parent / "fsr4_metal_work").resolve(), args.tiers)
    for tier in args.tiers:
        for up in (0, 1):
            if args.verify:
                verify(gen, gen_forced, src, out, tier, up, args.verify.resolve())
            else:
                _, recipe = build_native(gen, src, out, tier, up)
                gathered = sum(1 for r in recipe if r & GATHER)
                print(f"FSR 4.1.1 assets: {native_dir(tier, up)}: {len(recipe)} weight bytes, "
                      f"{gathered} from the initializer")


if __name__ == "__main__":
    main()
