# Credits and licenses

[← Documentation](README.md)

This project is licensed under the **GNU GPL v2 or later** ([LICENSE](../LICENSE)), like the
project it is based on.

## Based on

- [**deadinside28/bloodborne_pc**](https://github.com/deadinside28/bloodborne_pc) (bbport) by
  deadinside28: the native Linux port this project started from as a fork, and a separate
  repository since October 2026. Releases up to 0.51 are merged here with their history; parts
  of 0.6 are ported by hand (its renderer was rewritten). It provides the loader, the PS4
  runtime, the renderer extensions, the upscalers, the game-specific fixes and the original
  launcher. Code taken from it keeps its authorship in the git history; what came from which
  release is in [Upstream](UPSTREAM.md). From 0.6 (`8185b88`): the renderer fixes, the frame
  rate through hooks (`bbport_frame_rate.cpp`, `bbport_guest_code.h`), the game-code hook
  interface (`bbport_guest_code_hooks.h`, after shadPS4's InstallGuestCodeHook in the
  shadp2p/BBSC forks), Laurence's cinematic fixes, the live effects and the SSAO check
  (`bbport_game_hooks.cpp`, `bbport_effect_flags.h`), aspect ratios (camera, markers, UI viewport
  and HUD hooks, the UI edge stretch), the keyboard and mouse (`runtime_pad.c` and `window.cpp`
  merged from 0.6, `bbport_cursor.cpp`, native mouse look) and the menu pointer
  (`gpu/games/bloodborne_menu_pointer.cpp`; its findings first published in a fork of bbport).
- [**shadPS4**](https://github.com/shadps4-emu/shadPS4): the video core and shader recompiler
  (GPL-2.0+), and [sirit](https://github.com/shadps4-emu/sirit).
- **shadp2p** (a shadPS4 fork, GPL-2.0): the network and PSN libraries of the older online module
  (`gpu/bbnet`, shadNet), from bbport 0.51.
- The online module `gpu/bbnet20` (`libbbnet20`), from bbport 0.6 (`8185b88`) as Supermedo and
  Awaken put it together (its `SHADNET-PORT.md`): [shadPS4 v0.20.0](https://github.com/shadps4-emu/shadPS4/tree/v.0.20.0)'s
  network, NP and shadNet code (`529ceb3`, GPL-2.0-or-later);
  [**BBSC**](https://github.com/Classmg22/BBSC) seamless co-op by CLS_AMG (GPL-2.0-or-later,
  `bbsc/`) and its game server (`scripts/bbsc_gameserver/`, with No Dream For The Hunter's
  format, MIT); The Hunter's Dream backend from [droogie/bbhost](https://github.com/droogie/bbhost)
  (`566184a`, GPL-3.0-or-later, `thd/`), which makes `libbbnet20` as a whole GPL-3.0-or-later.
  Our changes for macOS are marked `bbport (macOS)` in its sources.

## From other forks

Changes taken from other ports of bbport, with thanks:

- [**Wooloomooloo2/bloodborne_mac_port**](https://github.com/Wooloomooloo2/bloodborne_mac_port)
  (GPL-2.0-or-later, `0b50589`) and [**Huax-1839/bloodborne-macos**](https://github.com/Huax-1839/bloodborne-macos)
  (GPL-2.0-or-later, `ee3120a`, through the former): the Metal backend's shader compiler, shader
  rewrites, pipeline-state cache and Vulkan-to-Metal tables (`video_core/renderer_metal/`), the
  pipeline-key split, the shared blend rules, the shadow check, the Vulkan-to-Metal command layer
  and frame encoder ([Native renderer](NATIVE_RENDERER.md)); from `4565225`, instanced tessellated
  draws drawn one instance at a time.

- [**Supermedo/bloodborne_pc**](https://github.com/Supermedo/bloodborne_pc/commit/79d44f0eecc2eed45c1e33b48996e53626adca48)
  (GPL-2.0-or-later, `79d44f0`, authored by Ambrose): the per-user trophy event persistence
  approach. Adapted here with a versioned title-aware protocol, checked file operations and
  edited-save eligibility. The notification overlay embeds the project's reviewed public
  catalog (names from PSN100 and Exophase, original requirement summaries); no game trophy
  files, game code or assets were taken.
- [**Supermedo/bloodborne_pc**](https://github.com/Supermedo/bloodborne_pc/commit/f33e7617bc1486dec0698786bd87baa40c02eab7)
  (GPL-2.0-or-later, `f33e7617`, authored by João Pedro Valadares): skip motion-vector
  graphics pipelines during cache preload. This extends the existing motion-shader rejection
  to keys whose cached metadata describes a plain permutation. Pipelines rebuild at first use.
- [**Supermedo/bloodborne_pc**](https://github.com/Supermedo/bloodborne_pc/commit/7e8bd6c1c85661cf0912531509ace5cf7cfc05a0)
  (GPL-2.0-or-later, `7e8bd6c1`, authored by devilismyfriend, committed by ayozetr): record
  guest-memory baselines before GPU image writes to prevent stale CPU uploads. Includes
  [Supermedo's `136a72d0`](https://github.com/Supermedo/bloodborne_pc/commit/136a72d0c2b79efc4f7d6f8e163ae6783c388d4a)
  limit on hashing large streamed textures. Adapted with explicit baseline validity, independent
  mip state and backing-view reads in precise-readback mode. Our existing whole-image hash-range
  fix from bmy/bbport-mac was retained. No game code or assets were taken.
- [**Supermedo/bloodborne_pc**](https://github.com/Supermedo/bloodborne_pc/commit/3a7cde8389e58423a9a33b41eb6380a0f497d3f4)
  (GPL-2.0-or-later, commit `3a7cde8`, authored by João Pedro Valadares): stable `SlotVector`
  storage and texture-cache reference reacquisition, following upstream shadPS4. Adapted with
  POSIX page protection inside the reserved mapping, fatal allocation checks and a normal-flow
  regression test. No game code or assets were taken.
- [**Supermedo/bloodborne_pc**](https://github.com/Supermedo/bloodborne_pc) (GPL-2.0-or-later),
  robustness fixes from 1.7.7–1.8.2, each written anew there from community pull requests:
  a stale texture format no longer stops the game (`3e3b760`, idea from parsifal295, PR #55);
  a garbage PM4 header drops the rest of its buffer (`c07f0ff`, parsifal295, PR #57); a draw
  whose vertex buffer table pointer is null is skipped (`2c4ddeb`); a flip IRQ ahead of its
  label waits briefly instead of stopping the game (`76f5b5f`, from Awaken's port).
- [**bmy/bbport-mac**](https://github.com/bmy/bbport-mac) (Rosetta 2 port of bbport to macOS):
  - the Retina-resolution window (`gpu/shim/window.cpp`, `BB_RETINA`);
  - the texture cache hashing whole images when checking a possibly changed one (the item
    picture on the loading screen).
- [**MaSieS4Fun/ARM_bloodborne_pc**](https://github.com/MaSieS4Fun/ARM_bloodborne_pc/commit/3d56243)
  (GPL-2.0, commit `3d56243`): the portable SRT walker and its `SrtOp` program for non-x86 hosts.
- [**ducvd89/Bloodborne-Android**](https://github.com/ducvd89/Bloodborne-Android/commit/bf1e6cf)
  (GPL-2.0, commit `bf1e6cf`): carried that walker to bbport 0.51 with shader-cache serialization
  and host-architecture separation; also supplied delayed condition-variable retirement and
  pitch-sized NV12 frame invalidation. Only these runtime changes were taken.
- [**bmy/bbport-mac**](https://github.com/bmy/bbport-mac/commit/0333d6004fd1499d6b4d029a3e6e5dd3c511f8ea)
  (GPL-2.0-or-later, commit `0333d60`): the MetalFX temporal upscaler
  (`video_core/renderer_vulkan/vk_metalfx.*`, `shim/bbport_metalfx.*`) — the
  `VK_EXT_external_memory_metal` bridge to the host `MTLDevice` and the submission-thread
  dispatch. Adapted to this tree (settings, menu, scheduler integration).

## Libraries

- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (Apache-2.0)
- [Zydis](https://github.com/zyantific/zydis) (MIT), the translator's x86 decoder
- [SDL3](https://www.libsdl.org/) (zlib)
- [FFmpeg](https://ffmpeg.org/) (LGPL)
- [Mesa](https://www.mesa3d.org/) KosmicKrisp (MIT, optional)
- [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) by FireBurn and the AMD FidelityFX SDK (MIT)
- [fsr4-native-macos](https://github.com/UzenUPoZiTiV4ik/fsr4-native-macos) by PoZiTiV4ik (UzenUPoZiTiV4ik) (MIT), native Metal backend for FSR 4.1.1
- [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) (MIT)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (Apache-2.0), the Metal backend's shader conversion
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) by Yuji Hirose (MIT), in the online module
- [half](https://half.sourceforge.net/) (MIT)
- DejaVu fonts
- [Inter](https://github.com/rsms/inter) by Rasmus Andersson and [Cinzel](https://github.com/NDISCOVER/Cinzel)
  by Natanael Gama (SIL Open Font License 1.1), the in-game menu's text and headings, as in the
  launcher
- The launcher: [Tauri](https://tauri.app), [React](https://react.dev),
  [shadcn/ui](https://ui.shadcn.com), [Tailwind CSS](https://tailwindcss.com),
  [i18next](https://www.i18next.com) (MIT)

## Game patches

By Kyo, Lance McDonald, auser1337, illusion, emoose and other community members
(`patches/Bloodborne.xml`).

## Disclaimer

This project is not affiliated with shadPS4, Sony Interactive Entertainment, FromSoftware or AMD.
*Bloodborne* is a trademark of Sony Interactive Entertainment. No game files are included.

The local platinum/base-DLC ID grouping follows the public launcher mapping in
[Supermedo/bloodborne_pc `79d44f0`](https://github.com/Supermedo/bloodborne_pc/commit/79d44f0eecc2eed45c1e33b48996e53626adca48)
by Ambrose (GPL-2.0-or-later), cross-checked against public trophy lists. The launcher credits
its public names/grade sources and original requirement summaries separately. The runtime
notification worker and SDL geometry bridge are project code; no trophy files/artwork or keys
were extracted or included.
