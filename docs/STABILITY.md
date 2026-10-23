# Stability

[← Documentation](README.md)

The crashes and corruption found so far, their causes and fixes; what is still open; and how
these were investigated. Run-by-run logs of the investigations are in this file's git history
(before 2026-10-20).

## Fixed

| Problem | Cause | Fix (switch to turn it off) | Since |
|---|---|---|---|
| GPU assertion while loading, `Unimplemented PM4 type 0` (most loads in Brazilian Portuguese) | The game's main thread and its render thread wrote one command buffer at once | A lock per command buffer around the game's command-buffer methods (`BB_CB_GUARD=0`); command buffers copied at submit | 0.5.1 |
| Opening video: a null read (address `0xd2`) before character creation, 2 in 5 openings | The render thread cleared the shared graphics input state between the movie's bind and draw | The movie frame and the render task lists hold the same buffer lock across their state changes and draws (`BB_MOVIE_STATE_GUARD=0`). 10 of 10 guarded openings completed, against 3 faults in 5 controls; about 0.5% more CPU | 0.6.0 |
| Opening video: audio thread panic `Invalid mutex` at shutdown | The AV player's queue read its size and front without its lock | Every queue access locked; tested under ThreadSanitizer. The playback clock's unsynchronized timestamp pair also fixed | 0.6.0 |
| Stretched scenery ("vertex explosions"), missing walls, Yharnam's black sky with streaks and objects popping in, after visiting several areas | MoltenVK draws wrongly from data past 4 GiB in one Vulkan buffer; the game's pool is 6 GB | The pool imported in windows so no binding offset reaches 4 GiB (`BB_POOL_SPLIT=0`) | 0.7.0 |
| GPU device lost (`VK_ERROR_OUT_OF_DEVICE_MEMORY`, "Invalid Resource") with character motion vectors, often in boss fights | GPU objects freed while command buffers still used them: residency-set removals, texture-cache deletions before the draw pipeline recorded its prepared draws, large staging buffers MoltenVK's argument buffers still referenced | Removals wait for the submissions that could use them; deletions one submission later; large staging buffers kept (`BB_STAGING_TRIM_LARGE=1`: trimmed) | 0.8.0 |
| 1440p and 4K stopped at the loading screen on an 18 GB Mac (device lost) | 9 GB of direct memory for every output above 1080p, imported twice (17 GB resident) | Memory sized from the scene; the pool imported once, in windows | 0.8.0 |
| A crash in MoltenVK (`isTessellationPipeline`) when a draw used a pipeline the driver could not create | The failed pipeline was skipped only the first time; later lookups returned it empty | Every lookup skips it | 0.8.0 |
| In-game restart froze | A thread blocked in `fgets` held stdin's lock and the restart's `fflush` waited for it | The launcher channel reads with `read(2)` | 0.8.0 |
| Stale texture formats, garbage PM4 headers, null vertex-buffer tables, flip interrupts ahead of their label stopped the game | Rare bad states | Each is logged and skipped (from the Supermedo fork) | 0.8.0 |
| Blur-kernel assertion `0xa0020007`; crash at `0x2199310` (GX command heap full) | Game data out of range; a 256 KiB per-frame heap | Logged and ignored; 1 MiB heaps (`BB_GX_HEAP=0`) (from bbport 0.6) | 0.8.0 |
| Metal recorder fault in `BindStage` while loading | Metal pipelines held shader pointers after Vulkan reused a module handle; queued draws held pipeline entries after deletion | Shader modules kept by SPIR-V content, bound pipeline entries shared through deferred draws | 0.8.1 |
| GX worker null access during a copied-save load, on Metal and MoltenVK | A live setting turned dynamic shadows back on in a secondary render context without a shadow target | Keep the game's disabled shadow state for that context while retaining the live setting elsewhere | 0.8.1 |
| Shader cache rebuilt at every launch after a rejected tessellation pipeline | The generated shader assigned one output location twice, and the rejected pipeline key was still saved; preload treated the driver rejection as a damaged cache entry | Emit each location once, rebuild old shader binaries once, and save only successful pipeline keys | 0.8.1 |
| Character clothing lost its lighting above about 3.5 million scene pixels on Metal and MoltenVK | The larger scene crossed the texture collector's cleanup mark sooner. It evicted small GPU-written images marked possibly CPU dirty without preserving their GPU contents | Keep GPU-written images whose contents cannot safely be downloaded until a definite CPU write supersedes them; normal collection of other images continues. Over the critical memory mark they are freed again (memory before lighting): a forced 10 GB budget at native 1440p darkened the clothing without a fault. Unforced native 1440p on the 18 GB M3 Pro stays about 2.4 GB below that mark | 0.8.2 |
| Metal API validation: the presented frame's view destroyed while a Metal path command buffer still needed it, below native render size with `BB_METAL_FRAME=1` (225–463 times in 75 s); no visible fault seen | The presenter made a view of the upscaler's display image every frame and freed it after the draw submission; the Metal path presents later | The view is cached with the display image and freed with it, after the GPU is idle (0 reports) | main |

## Open

- **Other native 1080p faults.** The repeatable GX worker fault was fixed above. A separate null
  guest virtual call was reported during an earlier Metal test but has not recurred. Its cause is
  unknown; a new occurrence needs its log and route.
- **GXRenderThread null read in Cathedral Ward.** Reported on 0.8.0 with MoltenVK in
  [app issue #3](https://github.com/bloodborne-native/bloodborne-mac/issues/3). Its signature
  differs from the fixed GX worker fault. It has not been reproduced or confirmed fixed; a
  nearby save and repeat steps are needed.
- **Pale blood after a save reload.** Blood on enemies already killed when the save is reloaded
  can appear pale with bright edges instead of dark red. The cause has not been isolated.
- **Missing objects or character parts after death reloads** on the translator, not under
  Rosetta 2 (2026-10-15): a candidate bbcpu translation bug; not seen since the pool fix, not
  retested separately.

### Watch list

Seen during development and not reproduced since; count each new occurrence with its build and
run:

- A short white flash in the opening video was seen in older runs but has not recurred recently.
  No fix has been confirmed.
- One enemy's body was drawn as colour noise after it attacked the player (reported 2026-10-19).
  It has not recurred recently; no fix has been confirmed.

| Message | Times | Last |
|---|---|---|
| `vk_shader_hle.cpp ExecuteCopyShaderHLE: Assertion Failed!` | 3 (once loading, twice in the video) | 2026-10-12 |
| `liverpool_to_vk.cpp MipFilter: Unreachable code!` | 5 (all in the opening video) | 2026-10-13 |
| `Unknown data_format=6 and num_format=1`, `=15 and =4` | 1 each | 2026-10-12 (now logged and skipped) |
| Movie audio thread `Invalid mutex (EINVAL)` at shutdown | 3 | 2026-10-13 (before the queue fix) |

The MipFilter assertion was caught at draw enqueue with valid table indices: the invalid sampler
comes from the table's contents, not from later corruption of the queued draw.

## Ruled out for the stretched geometry

Kept as a record so these are not tried again: FaceGen (disabling it still stretched), every
switchable bbport optimization, the draw pipeline, the copy hoist, GPU-completion submission
locking, texture-collector pressure, shader bounds checks, inline vertex fetch, primitive
restart and index alignment, copy-shader correctness, timing (5 FPS still stretched), cached
fetch shaders and file read retries. The data in memory was intact: draws reading past 4 GiB in
the pool were broken, all others correct.

## How to investigate

- Reproduce with a copied save (`tools/test_run.sh`, `SAVE_FROM=` a backup near the problem) or
  the opening video (`tools/test_opening_video.sh`), alternating a control with one change.
- A loading assertion or `Bad PM4 packet`: run `BB_CB_PROBE=1` first.
- A GPU device loss: Metal API validation (`MTL_DEBUG_LAYER=1
  MTL_DEBUG_LAYER_ERROR_MODE=nslog`); the window's BGRA8 sRGB drawable warning is noise.
- A fault in translated code: `BB_JIT_FAULT_MAP=1` names the exact guest instruction;
  `BB_JIT_WATCH` records selected loads and stores.
- Thread and lock problems: `BB_PTHREAD_PROBE=1` logs failing lock operations and the last mutex
  destructions; `BB_AVPLAYER_TIMING_PROBE=1` the video's timing; `BB_SAMPLER_PROBE=1` where an
  invalid sampler came from.
- Bisect bbport's optimizations with `BB_TOGGLE_FILE` (bits 0–19, 30, 32–50).
- A hang in an indirect dispatch: try `BB_INDIRECT_GUARD=0`.

Diagnostic output that contains game memory or addresses stays in the ignored `out/` folder,
never in Git or a wiki.
