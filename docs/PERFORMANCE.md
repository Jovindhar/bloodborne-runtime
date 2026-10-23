# Performance on macOS

[← Documentation](README.md)

Measured on an Apple M3 Pro (18-core GPU, 18 GB) with copied saves and alternating runs
([how](BUILDING.md#measuring)). The game is limited by the GPU; the CPU numbers matter for heat
and battery.

## Where it stands

| Place, 1080p output | Setting | FPS |
|---|---|---|
| Hunter's Dream | FSR 3.1 Balanced, MoltenVK | 58–68 |
| Hunter's Dream | FSR 3.1 Balanced, Metal renderer | 62–66 |
| Central Yharnam | FSR 3.1 Balanced, MoltenVK | 44–49 |
| The clinic at the start | Native 1080p (no upscaler) | ~31 |
| The clinic at the start | FSR 3.1 Balanced (renders 1130×636) | ~44 |

Character motion vectors cost about 6 FPS with FSR (36 against 42 in the clinic). The game's
code takes about 4–5 cores through the translator, and 20–30% more with the recompiled library
today (its speed-up is the recompiler's next milestone, R3).

## Where a frame's time goes

Native 1080p in the clinic: about 25 ms of GPU work and 6–8 ms of GPU idle per frame.

| GPU work | ms per frame |
|---|---|
| G-buffer pass (vertex ~2.6) | ~7.2 |
| Lighting | ~4.1 |
| Post-processing | ~5 |
| Compute dispatches | ~2.7 |
| Copies | ~2 |
| Separate clear passes | ~1.2 |
| Idle between Metal encoders (~630 encoders a frame) | 6–8 |

At 720p about 10 ms does not shrink with resolution: vertex work, shadow maps, small passes,
copies and idle. Every clear, tessellated draw and copy between draws ends a Metal render pass,
and on Apple's tile-based GPUs each new pass reloads its attachments; that idle time is the
largest single item, and the main target of the [native renderer](NATIVE_RENDERER.md).

## What helped

| Change | Effect |
|---|---|
| No SPIR-V `NoContraction` (SPIRV-Cross made unoptimizable helpers of it in every shader) | 16.7 → 30.4 FPS |
| GPU memory kept in a Metal residency set | Steady frames instead of 400–650 ms hitches |
| Upscaler presets render the whole game smaller from the start | ~31 → 37–44 FPS with FSR |
| Character motion vectors off by default | FSR 36 → 42 FPS |
| Color clears deferred to the next pass's load action (`BB_DEFER_CLEARS`) | +1–2 FPS in three pairs |
| Copies recorded before a render pass instead of splitting it (`BB_HOIST_COPIES`) | ~1–2 ms |
| Larger direct memory only for outputs above 1080p, sized from the scene | A 9 GB pool ran the clinic at 7 FPS on 18 GB |
| The Metal renderer (`BB_METAL_FRAME=1`) | 65.5/61.7 against 58.0/58.1 FPS, p90 16–18 against 19–20 ms |

MetalFX is an optional macOS upscaler. In one copied-save scene at 1080p Balanced on the Metal
renderer, FSR 3.1 had a 21.3 ms median and 28.3 ms p90 frame time; a warm MetalFX run had
23.9 ms median and 28.8 ms p90. The extra copies and submission gave no speed advantage on this
route. Both upscalers rendered it correctly; image quality is a separate choice.
| Frame rate through hooks instead of patches | No cost: 66.6–68.0 against 65.0–67.3 FPS |

## Measured without gain

- Fast transcendental math; MoltenVK fast math, prefilled command buffers, more command buffers,
  Metal heaps, two frames ahead, anisotropic filtering off.
- MoltenVK without Metal argument buffers: about 85% slower here (the Wooloomooloo2 fork measured
  a gain on its build).
- Skipping draws that render nothing; grouping hoisted copies by buffer pair.
- Rect lists as triangles, and fixed-quad tessellation as triangles (`BB_FIXED_QUAD_TRIANGLES=1`):
  fewer compute encoders but 30–38 points more CPU and a worse p90. Native Metal tessellation
  replaces it.
- Cheaper motion vectors (smaller target, no history stores): the cost is the extra vertex
  outputs.
- Texture upload baselines (`BB_IMAGE_UPLOAD_BASELINE=1`, from the Supermedo fork): slightly
  slower uncapped, equal capped; stays opt-in.
- KosmicKrisp (Mesa's Vulkan on Metal 4): correct, about 3× slower than MoltenVK.

## Made things worse

- Images that allow views in other formats (`VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`) where none are
  needed (Metal may then leave them uncompressed). The upscaler's images made so cost MetalFX
  about 2.5% of frame time (30.9 against 30.1 ms median).
- A CPU fence on swapchain acquire.
- Waiting on the CPU for each frame before presenting it: crashes while a save loaded.

## Where more could be gained

1. Idle between Metal encoders (6–8 ms a frame): fewer pass breaks, in the Metal renderer.
2. Vertex work (~6 ms at native): shaders read constants one value at a time from storage
   buffers; constant buffers might let Metal preload them.
3. FSR's overhead (~3 ms over a plain upscale).
4. MetalFX as an upscaler (from the Wooloomooloo2 fork), measured first.

Measuring tips: MoltenVK's GPU timestamps do not work and `ioreg`'s utilization overstates how
busy the GPU is; use Metal System Traces.
