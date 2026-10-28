# Performance on macOS

[← Documentation](README.md)

What a frame costs, what helped, what did not, and where more could be gained. Measured on an
Apple M3 Pro (18-core GPU, 18 GB) with copied saves and alternating runs
([how](BUILDING.md#measuring)), 1080p output. Every figure names its renderer, upscaler and place.
The default is the **native Metal renderer with the upscaler off**. The game is limited by the
GPU; CPU time matters for heat and battery, and on weaker CPUs.

## Where it stands

The default (native Metal, upscaler off, native 1080p):

| Place | FPS |
|---|---|
| Cathedral Ward, standing (the test save; about 1,660 draws a frame) | 38.2–39.6 |
| A new game, 3 h 26 min of play by the maintainer | median 42.2; 37.2 or more 90% of the time |
| Hunter's Dream | not measured |

For comparison:

| Place | Renderer, upscaler | FPS |
|---|---|---|
| Cathedral Ward | MoltenVK, none | 30.6 |
| Cathedral Ward | MoltenVK, FSR 3.1 Balanced | 27.6 |
| Cathedral Ward | Metal, MetalFX Balanced / Quality | 33.5 / 31.7 |
| Hunter's Dream | MoltenVK, FSR 3.1 Balanced | 58–68 |
| Hunter's Dream | Metal's first version (before the render adapter), FSR 3.1 Balanced | 62–66 |
| Central Yharnam | MoltenVK, FSR 3.1 Balanced | 44–49 |
| The clinic at the start | MoltenVK, none | about 31 |
| The clinic at the start | MoltenVK, FSR 3.1 Balanced (renders 1130×636) | about 44 |

Native Metal is about 25–30% faster than MoltenVK at native 1080p. FSR is not offered on Metal
(MetalFX only, [decision 5](RENDER_ADAPTER.md#decisions)); FSR 3.1 and FSR 4.1.1 run only with
the MoltenVK fallback.

**Why the upscaler is off by default.** On Metal at 1080p the frame is not bound by pixel work:
in Cathedral Ward MetalFX Balanced renders 44% of the pixels and runs at 33.5 FPS against native
39.0, and FSR 3.1 Balanced (when the Metal renderer still offered it) ran at 36.2. The
upscalers are for outputs above 1080p, where native resolution needs too much memory.

## CPU

- **The game's code** takes about 3–5 cores through the translator, and about 14% less with the
  recompiled library at the same frame rate: 344% of one core against 402% at the 1st Floor
  Sickroom lamp, 357% against 392% in Cathedral Ward. The recompiler's milestone R3 (its speed)
  is done; its CPU work continues after N6 ([roadmap](NATIVE_RENDERER.md#now-and-next)).
- **The renderer's GPU thread** records a draw in about 4.0–4.4 µs on Metal.
- On MoltenVK (Hunter's Dream, about 48 FPS): the game's code about 3.8 cores, the renderer
  about 1.2, MoltenVK and Metal about 0.6. Not measured the same way on native Metal.

## Where a frame's time goes

No Metal System Trace breakdown of the native Metal renderer has been recorded yet. The two on
record are MoltenVK's:

- **The clinic, MoltenVK, native 1080p:** about 25 ms of GPU work and 6–8 ms of GPU idle a frame
  in about 630 Metal encoders.

  | GPU work | ms per frame |
  |---|---|
  | G-buffer pass (vertex about 2.6) | about 7.2 |
  | Lighting | about 4.1 |
  | Post-processing | about 5 |
  | Compute dispatches | about 2.7 |
  | Copies | about 2 |
  | Separate clear passes | about 1.2 |
  | Idle between Metal encoders | 6–8 |

- **Hunter's Dream, MoltenVK, about 48 FPS:** 14.9 ms of GPU work and 5.9 ms of idle (28% of the
  frame) in about 420 Metal encoders.

At 720p about 10 ms did not shrink with resolution: vertex work, shadow maps, small passes,
copies and idle. Every clear, tessellated draw and copy between draws ended a Metal render pass,
and on Apple's tile-based GPUs each new pass reloads its attachments.

## What helped

| Change | Renderer, upscaler, place | Effect |
|---|---|---|
| The native Metal renderer | Cathedral Ward, none | 38.2–39.6 FPS against 30.6 on MoltenVK |
| The Metal renderer's first version (`BB_METAL_FRAME=1`) | Hunter's Dream, FSR 3.1 Balanced | 65.5 and 61.7 FPS against 58.0 and 58.1 on MoltenVK; p90 16–18 ms against 19–20 |
| MetalFX encoded on the native frame's command buffer instead of the export bridge | Metal, MetalFX, copied save (one pair) | 34.5 against 30.8 mean FPS |
| No SPIR-V `NoContraction` (SPIRV-Cross made unoptimizable helpers of it in every shader) | MoltenVK | 16.7 → 30.4 FPS |
| GPU memory kept in a Metal residency set | MoltenVK and Metal | Steady frames instead of 400–650 ms hitches |
| Upscaler presets render the whole game smaller from the start | MoltenVK, FSR 3.1, the clinic | about 31 → 37–44 FPS |
| Character motion vectors off by default | MoltenVK, FSR 3.1, the clinic | 36 → 42 FPS |
| Colour clears deferred to the next pass's load action (`BB_DEFER_CLEARS`) | MoltenVK | +1–2 FPS in three pairs |
| Copies recorded before a render pass instead of splitting it (`BB_HOIST_COPIES`) | MoltenVK | about 1–2 ms |
| Larger direct memory only for outputs above 1080p, sized from the scene | MoltenVK, the clinic | A 9 GB pool had run the clinic at 7 FPS on 18 GB |
| The recompiled library | Metal, Cathedral Ward | 14% less CPU time, same frame rate |
| Frame rate through hooks instead of patches | Not recorded | No cost: 66.6–68.0 against 65.0–67.3 FPS |

The MetalFX upscaler came from [bmy/bbport-mac](https://github.com/bmy/bbport-mac) (commit
`0333d60`, [credits](CREDITS.md)).

## Measured without gain

- Fast transcendental math; MoltenVK fast math, prefilled command buffers, more command buffers,
  Metal heaps, two frames ahead, anisotropic filtering off.
- MoltenVK without Metal argument buffers: about 85% slower here (the Wooloomooloo2 fork measured
  a gain on its build).
- Skipping draws that render nothing; grouping hoisted copies by buffer pair.
- Rect lists as triangles, and fixed-quad tessellation as triangles (`BB_FIXED_QUAD_TRIANGLES=1`,
  MoltenVK): fewer compute encoders but 30–38 points more CPU and a worse p90. The Metal renderer
  uses Metal's tessellator instead.
- Cheaper motion vectors (smaller target, no history stores): the cost is the extra vertex
  outputs.
- Texture upload baselines (`BB_IMAGE_UPLOAD_BASELINE=1`, from the Supermedo fork): slightly
  slower uncapped, equal capped; stays opt-in.
- KosmicKrisp (Mesa's Vulkan on Metal 4): correct, about 3× slower than MoltenVK.
- An upscaler at 1080p on Metal (above).

## Made things worse

- Images that allow views in other formats where none are needed (Metal may then leave them
  uncompressed): the upscaler's images made so cost MetalFX about 2.5% (30.9 against 30.1 ms
  median).
- Driver queries on every submission (`ReportMemory` per guest submission: 5% of the GPU thread's
  time per draw); copying binding writes before recording them (about 4%); recording a whole
  80-byte draw command (4.2 → 5.8 µs per draw). Details in [Render adapter](RENDER_ADAPTER.md#what-was-learnt).
- A CPU fence on swapchain acquire.
- Waiting on the CPU for each frame before presenting it: crashes while a save loaded.

## Where more could be gained

In the order of the [native renderer's plan](NATIVE_RENDERER.md#now-and-next):

1. **N6, the engine's draws taken at their source** instead of decoding the PS4 command buffers
   the game writes: less work for the renderer's CPU threads per draw. Not measured yet.
2. **The recompiler's CPU work** (bloodborne-recomp): less CPU time for the game's code, most
   useful on CPUs weaker than the M3 Pro's.
3. **N2, shaders converted ahead of time:** no shader conversion or pipeline build during play
   (Metal's binary archive already keeps built pipelines between sessions).
4. **N3, textures as objects:** the engine's own texture creation instead of inferring images
   from GPU memory.
5. **GPU time on Metal.** First record a Metal System Trace of the native renderer; on MoltenVK
   the largest items were idle between encoders (6–8 ms in the clinic, from pass breaks) and
   vertex work (shaders read constants one value at a time from storage buffers; constant
   buffers might let Metal preload them).

Measuring tips: MoltenVK's GPU timestamps do not work and `ioreg`'s utilization overstates how
busy the GPU is; use Metal System Traces.
