# Architecture

[← Documentation](README.md)

How the runtime runs Bloodborne (PS4, CUSA03173, update 1.09) on a Mac: what each part does,
where its code is, and the switches that matter when working on it. Building and testing are in
[Building](BUILDING.md).

## Overview

```
eboot.bin ──► loader (src/probe.c) ──► game image in memory
                                         │
             game's x86-64 code ─────────┤  bbcpu (recomp/): translated to arm64 as it runs,
                                         │  or the recompiled library (BB_RECOMP_LIB)
                                         │
             PS4 system calls ───────────┤  runtime (src/runtime_*.c): memory, threads, files,
                                         │  saves, pad, audio, trophies, video out
                                         │
             GPU command buffers ────────┘  renderer (gpu/): PM4 packets and GCN shaders
                                            → Vulkan → MoltenVK → Metal
                                            (or Metal directly: BB_METAL_FRAME=1)
```

`run.sh` prepares a launch (checks the game folder, links the modules, writes patches, sizes
memory) and starts `out/bb-probe`. The launcher (bloodborne-launcher) sets its environment from
the player's settings.

## Loading the game

`scripts/prepare.py` and `scripts/link_modules.py` turn the dump's `eboot.bin` and its system
modules into one flat image; the PS4's libc and libSceFios2 are linked into it as game code.
`src/probe.c` maps that image at `BB_IMAGE_BASE` (on macOS inside the guest address range that
`src/platform.c` reserves at startup), applies relocations and imports, then the patches
(`scripts/patches.py` from `patches/Bloodborne.xml`, the community's patch collection), then the
runtime's own hooks (`bbgpu_patch_image`), and finally starts the game's entry point.

`scripts/game_check.py` refuses any executable other than 1.09: every address in the runtime is
an address in that executable.

## CPU: bbcpu and the recompiled library

The game's code is x86-64. On Apple Silicon it runs through **bbcpu**
([bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp), `recomp/src/cpu/`):
blocks are translated to arm64 the first time they run, with an interpreter as the fallback.
Guest memory is host memory (identity mapped). A guest call to an address outside the game's code
is a call of a host function, with the System V arguments bridged; host code calls game functions
with `runtime_guest_call` (`bbcpu_call` underneath). Details:
[Native arm64](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/ARM64_NATIVE.md).

`BB_RECOMP_LIB=recomp/out/recomp/librecomp.dylib` loads the **recompiled library** instead: the
game's functions generated as C from the player's own executable and compiled ahead of time. A
function whose bytes were changed by a patch or hook stays with the translator. The library is
built locally and never committed (it is game code). Details:
[Recompilation](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/RECOMPILATION.md).

## The PS4 runtime

`src/runtime_*.c` implements the system functions the game imports: memory
(`runtime_memory.c`), threads and locks, files (with mods layered over the game folder), saves,
the pad (SDL3 controllers and the keyboard), audio (AT9 through LibAtrac9), video playback
(AvPlayer on FFmpeg), the clock, trophies (`TROPHIES.md`) and the network stubs. Platform
differences (Linux and macOS) are in `src/platform.{c,h}`.

**Memory.** Direct memory is 5,056 MiB as on the PS4. Outputs above 1080p need more for their
larger targets: `run.sh` sets `BB_DMEM_MB` to 5,056 MiB plus 448 bytes per scene pixel above
1080p, in 64 MiB steps. `BB_DMEM_EXTRA_MB` (512 by default) adds memory past the game's size that
only the runtime hands out (Laurence's render cache, below). Direct and flexible memory live in
one shared-memory pool that the GPU uses in place.

**Command buffers.** The game's main thread and its render thread write the same graphics command
buffer while an area loads. `src/runtime_cmdbuf.c` holds a lock per buffer around the game's
command-buffer methods (bbcpu guards; `BB_CB_GUARD=0`: off), and around the opening video's frame
and the render thread's task lists (`BB_MOVIE_STATE_GUARD=0`: off). `BB_CB_PROBE=1` names any
two threads that use one buffer at once.

## Hooks in the game's code

Some features change what the game does at a given instruction (from bbport 0.6:
`gpu/shim/bbport_frame_rate.cpp`, `bbport_game_hooks.cpp`). A hook replaces whole instructions
with a jump to a small stub `int3; the replaced instructions; jmp back`. bbcpu translates the stub
like the game's code; its `int3` reaches the runtime's handler with every register, which the
hook may change (`Core::InstallGuestCodeHook`, bbport's interface). Each site is checked byte for
byte first, so another game version leaves it alone. Code the runtime rewrites while the game
runs is excluded from the recompiled library.

| Hook | What it does | Off |
|---|---|---|
| Frame rate | Above 30 FPS the game's frame pacer and main loop step by each frame's real time, and 48 places tied to a 30 FPS frame follow it (physics timers, damping, cloth, the run's wall check). The limit applies live; **F3** toggles 30 FPS. | `BB_FRAME_HOOKS=0` (the community patches instead) |
| Effects | Chromatic aberration, depth of field, motion blur, SSAO (only where the game has its target), the game's anti-aliasing, dynamic-light shadows and bloom switch without a restart. Screen-space reflections stay a patch at start. | `BB_HOT_EFFECTS=0` |
| Aspect ratio | On an output that is not 16:9 the camera's aspect ratio follows it (wider sees more to the sides, taller more above and below), the HUD's markers stay over their characters, the UI's viewport has the output's shape and the HUD's groups move to the edges (Scaleform members set through the game's own functions). Full-screen UI fills reach the output's edges (a vertex-shader snap, `EmitUiEdgeStretch`). | `BB_HUD_ANCHOR=0` (HUD in the middle) |
| Mouse | Native mouse look: the mouse's turn is added to the camera's own turn each update (no stick dead zone, curve or speed limit); in lock-on and menus the mouse drives the right stick. A pointer in the game's menus (Scaleform lists): hover selects, left click confirms, right click goes back, the wheel scrolls (`gpu/games/bloodborne_menu_pointer.cpp`). | `BB_NATIVE_MOUSE=0`, `BB_MENU_POINTER=0` |
| Blur-kernel clamp | The blur kernel's base sample count raised to 3 (2 stalled the game: a black screen). | `BB_KERNEL_CLAMP=0` |
| Laurence's cinematic | Failed render jobs give their pool slot back; the render resource cache gets 512 MiB of the runtime's memory instead of 160 MiB. Above 30 FPS the cinematic hung or lost models without these. | `BB_JOB_SLOT_FIX=0`, `BB_RENDER_RESOURCE_CACHE_MB=160` |
| Resource copies | The GPU side hears of the game's resource uploads (new memory model only). | `BB_GUEST_HOOKS=0` |

`BB_GAME_HOOKS=0` turns off every hook but the frame rate's.

## Graphics

The renderer (`gpu/shadps4/`) is derived from [shadPS4](https://github.com/shadps4-emu/shadPS4)
and extended by bbport. It reads the PM4 packets the game writes into its command buffers,
translates the GCN shaders to SPIR-V, and draws with Vulkan. On macOS,
[MoltenVK](https://github.com/KhronosGroup/MoltenVK) runs that Vulkan on Metal.

**The draw pipeline.** Draws are prepared in two stages: stage A reads packets and resolves the
draw's resources, partly on preparation workers (`BB_PREP_WORKERS`); stage B, the GPU thread,
records them. `BB_DRAW_PIPE=0` runs both in one thread. Most of bbport's speed-ups can be switched
off at run time for bisection: `BB_TOGGLE_FILE` names a file holding a bit mask (re-read every
250 ms).

**Memory on the GPU.** The game's memory pool is imported as Vulkan buffers, so the GPU reads the
game's data where it is. MoltenVK draws wrongly from offsets past 4 GiB in one buffer, so the pool
is imported in overlapping windows (a new one every 3 GiB, each 3.5 GiB long) and every binding
uses a window where its offset is below 4 GiB (`BB_POOL_SPLIT=0`: one import). GPU objects stay
in a Metal residency set (`gpu/shim/bbport_metal_residency.*`); removed ones are kept until the
GPU has finished every submission that could use them.

**Resolution and upscaling.** The output has an aspect ratio (16:9 by default; 4:3, 16:10, 21:9, 32:9 or Auto, the monitor's) and a size from that ratio's list (`aspect_ratio`, `output_res`). The game renders at 1920×1080 internally. With an upscaler preset,
a patch at start makes the whole game render smaller (for example 1130×636 for Balanced at 1080p)
and AMD FSR 3.1 reconstructs the output resolution; the game's UI is drawn at the output size.
On macOS, MetalFX or FSR 4.1.1 on Metal (`gpu/fsr4_metal/`, from fsr4-native-macos) can do it
instead. FSR 4.1.1 runs on MoltenVK's own textures in a Metal queue of its own, ordered against
the Vulkan work by shared events; its kernels and model layouts are built with the runtime
(`gpu/fsr4_metal/build_assets.py`), and AMD's initializers stay in the player's folder
(`BB_FSR411_DIR`; `tools/fsr4_metal_initializers.sh` takes them from the player's own AMD DLL):
the menu offers it only when both are there. On an M3 Pro it costs 4.8 ms of GPU time a frame at
1080p output (Balanced), about 13% of the frame rate against FSR 3.1.
The other route, live resolution, keeps the game at 1920×1080 and scales the renderer's targets
while it runs (`live_resolution` in the settings).

**Character motion vectors** (off by default on macOS): the game has no motion vectors for
moving characters, so a variant of their vertex shaders writes them, which makes FSR much sharper
on characters for a few frames per second.

**The Metal renderer** (experimental, the launcher's Advanced → Renderer, `BB_METAL_FRAME=1`): a
layer under vulkan.hpp's dispatcher (`gpu/shadps4/video_core/renderer_metal/`) encodes the
command buffers itself in Metal instead of passing them to MoltenVK. The renderer above it is
unchanged, so Linux keeps real Vulkan. Pipelines are converted from SPIR-V to MSL with
SPIRV-Cross. Resources are still created by MoltenVK. Plan and progress:
[Native renderer](NATIVE_RENDERER.md).

## Settings, the menu and the launcher

`gpu/shim/bbport_settings.*` holds the game settings (`bbport.ini`) and the launcher's own options
the menu shows (fullscreen, frame rate, renderer, ...). The launcher keeps the settings in its
database; while the game runs the two exchange changes as lines on the game's standard streams:
`BBSET key=value` (game settings) and `BBSET launcher:key=value`. The game reads them with
`read(2)` on its standard input.

The in-game menu (`gpu/shim/bbport_overlay.cpp`, Dear ImGui) opens with **F1** or **L3 + R3**. It
has the launcher's tabs and look, applies what it can at once, and offers a restart for the rest
(the launcher's options are exported to the environment again first). Its texts are the
launcher's locales (`gpu/shim/locales/`). The FPS counter's big size names the renderer and the
CPU path.

Trophy unlocks are written to the profile's `trophies-v1.log`; a separate launcher helper shows
the notifications ([Trophies](TROPHIES.md)).

## Where it comes from

Almost everything that makes the game run (the loader, the PS4 runtime, the renderer's
extensions, the upscalers and the game patches) comes from
[bbport](https://github.com/deadinside28/bloodborne_pc), the Linux port this project started from,
and from shadPS4. This project adds the macOS platform layer, the arm64 translator and the
recompiler, the Metal work and the launcher integration. What came from which bbport release and
fork: [Upstream](UPSTREAM.md).
