# Native renderer (milestone R5)

[← Documentation](README.md)

**Goal:** the game's graphics drawn with each platform's own GPU API, with nothing translated
while the game runs: no MoltenVK on macOS, no shaders compiled during play. The renderer stays
multiplatform: one shared core, and thin backends for Vulkan (Linux, Windows) and Metal (macOS).
It is the graphics half of the native port; the CPU half is the
[recompiler](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/RECOMPILATION.md).

## Principles

1. **Multiplatform.** Nothing works on Metal only. Planning a frame, tracking resources and
   converting shaders happen in the shared core; a backend only speaks its API.
2. **Always playable.** Native Metal is the default on Apple Silicon; the MoltenVK renderer stays
   selectable until M4 is accepted.
3. **Measured.** Each step is compared against the path it replaces: FPS, frame-time p90, CPU,
   and GPU time from Metal System Traces.
4. **Ahead of time where possible.** Shaders and texture layouts converted once, on the player's
   machine, from their own copy of the game.

## Progress

Each bar has 20 blocks, one per 5% of the step's accepted scope.

| Step | Progress | State |
|---|---|---|
| N0 · scope | 100% `████████████████████` | Where the commands come from and where a frame's time goes |
| N1 · frame planning | 100% `████████████████████` | Deferred clears accepted; tessellation moved to N5 |
| N2 · shaders ahead of time | 0% `░░░░░░░░░░░░░░░░░░░░` | Next: inventory the shader variants |
| N3 · textures as objects | 0% `░░░░░░░░░░░░░░░░░░░░` | Next: trace texture creation |
| N4 · render adapter | 100% `████████████████████` | [Specification](RENDER_ADAPTER.md) approved; A0–A6 done: the renderer runs on the adapter with the Vulkan backend; the Metal backend continues as N5 (M1–M4) |
| N5 · Metal backend | 95% `███████████████████░` | M1–M4 are implemented, including loader-free native startup; M1/M2 capability comparisons and the maintainer's release-candidate play session remain for acceptance |
| N6 · engine render boundary | 0% `░░░░░░░░░░░░░░░░░░░░` | The boundary is found and measured (R4, below); next: the shader resources; the draw state's layout is partly known (R4.3) |

## What was measured (N0, N1)

- **The command stream.** Bloodborne links libGnm statically: the game writes the PS4's command
  packets (PM4) itself, through 99 known libGnm methods, and the packets record those calls
  without loss. The packet reader stays the front end.
- **A frame on macOS** (Hunter's Dream, ~48 FPS, MoltenVK): 14.9 ms of GPU work, 5.9 ms of GPU
  idle (28% of the frame) in about 420 Metal encoders. Every clear, tessellated draw (MoltenVK
  runs the vertex and hull stages as a compute pass) and copy between draws ends a render pass.
  CPU: the game's code ~3.8 cores, the renderer ~1.2, MoltenVK and Metal ~0.6.
- **Decisions.** Eligible clears become the next pass's load action (on by default,
  `BB_DEFER_CLEARS=0`: off; +1–2 FPS). Drawing fixed-factor tessellation as triangles
  (`BB_FIXED_QUAD_TRIANGLES=1`) cost more CPU and p90 than it saved: left opt-in; Metal's own
  tessellator does it in N5. Grouping copies gained nothing.

## The engine's render boundary (N6, from R4)

Dantelion2's CoreGraphics2 draws through seven methods of its graphics context (`draw`,
`drawInstanced`, `drawIndexed`, `drawIndexedInstanced`, `drawIndirect`, `drawIndexedIndirect`,
`dispatch`); each applies the state blocks of the draw state it is given, binds the shader
resources and calls libGnm (bloodborne-recomp's RECOMPILATION.md, "Annotations"). Counted with
`BB_DRAW_CENSUS=1` (2026-10-28, M3 Pro, translator, the player's save in a level, about 62,000
libGnm draws a second at 33 FPS, about 1,880 a frame):

| Through | Share of libGnm's draws and dispatches |
|---|---:|
| The seven methods (`drawIndexed` 33,000/s, `draw` 17,800/s, `drawIndexedInstanced` 1,800/s, `drawIndirect` 100/s) | 85% |
| Render-target passes (fast-clear elimination, depth-stencil clears, depth decompression) | 2% |
| CoreGraphics2's compute passes (the dispatches, about 4,400/s, but for the few the render-target passes make; the same context and resource binder) | 7% |
| Scaleform's renderer (menus and HUD) and one small wrapper outside CoreGraphics2 | 6% |

So N6 can take nearly all of the world's draws at the seven methods; compute passes, render-target
passes and Scaleform are three smaller boundaries of their own.

Who calls the seven methods (a second run, 2026-10-28, the 16 busiest call sites): 65% of the calls
come from CoreGraphics2's mesh draw (`CG::MeshRenderer::drawMesh`: one draw per face set of a
mesh, with the mesh's vertex buffers and input layout), then three functions with 18%, 7.5% and
5.9%.

## Design

- **Shared core:** the packet reader and bbport's parallel draw preparation; frame planning
  (merging what splits passes); resources (shadPS4's texture and buffer caches and detiling);
  shaders (GCN → shadPS4's intermediate form → SPIR-V or MSL).
- **Backends:** Vulkan (today's code) and Metal; D3D12 possible later.
- **Where Metal meets the core today:** at the Vulkan calls the renderer already makes. A layer under
  vulkan.hpp's dispatcher (`renderer_metal/mtl_vk_layer`) takes the command buffers the Metal path
  owns and encodes them in Metal (`mtl_frame`); MoltenVK gets the rest. The renderer above is
  unchanged and Linux keeps real Vulkan. Vulkan's enum values are the shared vocabulary of the
  pipeline keys (`vk_pipeline_key.h`) and blend rules; no Vulkan handle is in them.

**Replaced by the [render adapter](RENDER_ADAPTER.md)** (approved 2026-10-22): the renderer
calls the project's own GPU interface, with Vulkan and Metal backends. Native Metal no longer
loads MoltenVK; the selectable fallback remains until M4 acceptance. The interception route was
chosen on 2026-10-20 over N4's original order (a backend-neutral command stream
first, then Metal): adapting the Metal renderer of
[Wooloomooloo2/bloodborne_mac_port](https://github.com/Wooloomooloo2/bloodborne_mac_port)
reached a working Metal frame directly. N4's approved contract still describes how resources and
synchronization should be owned:

- opaque, generation-checked handles for buffers, images, views, samplers, shaders, pipelines;
  physical release deferred to the completion of the last submission that could use them;
- flat, typed binding slots per stage (as Vulkan push descriptors and Metal's direct slots), no
  shared descriptor tables or argument buffers;
- the core declares each pass's and command's reads and writes; each backend chooses its own
  layouts, barriers or encoder boundaries;
- one backend chosen at startup; no mixed-backend frame once resources move to Metal.

## The Metal backend today (N5)

The shader compiler, shader rewrites, pipeline cache and format/state tables originated in
[Huax-1839/bloodborne-macos](https://github.com/Huax-1839/bloodborne-macos/commit/0f0cf54).
They reached us through Wooloomooloo2/bloodborne_mac_port (`0b50589`, `78b59f5`), which further
developed the Metal command layer and frame encoder. We adapted these parts into the shared
renderer; both source renderers are Metal only.

- **Shaders and pipelines:** SPIR-V to MSL with SPIRV-Cross (direct buffer, texture and sampler
  slots); rect lists rewritten as triangles; tessellation as compute kernels plus Metal's
  tessellator; geometry shaders as kernels; pipeline states cached in a binary archive between
  sessions (`PipelineFactory`).
- **The frame:** render passes, draws, dispatches, copies, clears and blits encoded in Metal,
  with push descriptors bound to the shaders' slots, on Metal's own queue. Its images, buffers
  and synchronization are native Metal objects.
- **Switches:** `BB_METAL_FRAME=1` (the native Metal default), `BB_METAL_FRAME=0` (MoltenVK
  fallback), `BB_METAL_TRACE=1` (command counts).

| Measured (Hunter's Dream, 1080p, FSR Balanced, M3 Pro) | FPS | p90 |
|---|---|---|
| MoltenVK, two runs | 58.0, 58.1 | 20.4, 18.9 ms |
| Metal, two runs alternating with them | 65.5, 61.7 | 16.3, 17.6 ms |
| Metal with the recompiled library, one run | 51.7–57.3 | |

Every pipeline the game uses builds in Metal (550: 510 graphics, 40 compute; 0 failed). The
frame path met 0 draws without a Metal pipeline and 0 unsupported commands. An earlier full play
session completed; a later native 1080p play session crashed on a GX worker. A copied-save load
aborted in Metal's fragment binding; `e0348c2` retains the shader bindings and pipeline entries
through deferred recording. The same load then reached a worker fault on both Metal and MoltenVK:
the live dynamic-shadows setting overrode a secondary context's disabled state. The 0.8.1 guard
keeps that state. Recording costs a little more per draw on the GPU thread
(5.3–5.6 against 5.1–5.2 µs). The character-lighting loss above 3.5 million scene pixels
also happened on Metal; the texture collector now retains unresolved GPU-written images.

MetalFX is available as an optional temporal upscaler on macOS with either renderer. It uses the
Vulkan driver's Metal device through `VK_EXT_external_memory_metal`; the input and output copies
are still needed until the renderer creates its images directly in Metal. On the M3 Pro's copied
save at 1080p Balanced, it ran without a fault but did not beat FSR 3.1 on frame time
([measurements](PERFORMANCE.md#what-helped)).

## Next

1. The [render adapter](RENDER_ADAPTER.md): steps A0–A6 move the renderer onto it with the
   Vulkan backend; M1–M4 build the Metal backend on it (resources in Metal, as the fork's "M2"
   `3ee456f` did) and leave MoltenVK out on macOS.
2. Shaders converted ahead of time (N2): MSL is still converted while the game runs.
3. With the recompiled library: calls of hooked engine functions must go through the runtime
   (recompiled functions call each other directly), before N3 can hook texture creation.
