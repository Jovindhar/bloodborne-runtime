# Yharnam Together on BloodOnline (gpu/ytcoop)

BloodOnline's third online mode is Yharnam Together, the Bloodborne seamless co-op by lKaniel,
AMANISES and Supermedo (<https://github.com/Amanises-Yharnam-Together/BBSC-Yharnam-Together>,
GPL-2.0-or-later). Its own release is a Windows build of bbport; this directory builds its
co-op code as this runtime's online module, `libytcoop.dylib`, loaded instead of `libbbnet20`
when the launcher's Online page is set to Yharnam Together (`BB_NET_BACKEND=ytcoop`, run.sh).

## What is vendored

`vendor/` holds the files of Yharnam Together **v1.4.0** (tag `v1.4.0`, commit `efa0ad2`) that the
module compiles or includes, at their paths in that repository, with its LICENSE files and
README. They were picked from the build's dependency list, not by hand. The changes are in
`vendor-changes.diff`, all for macOS on Apple Silicon:

- `bbport/gpu/bbcoop/memory.cpp` gains a macOS `HostRegion` (`mach_vm_region`), where the Windows
  build used `VirtualQuery` and Linux `/proc/self/maps`. A region that says it is readable can still
  fault on macOS (the runtime's shared memory pool past its file's size, GPU mappings: SIGBUS), so
  the address is read through the kernel first (`mach_vm_read_overwrite`) and only its page is
  reported. Without it, a stale enemy pointer (`EnemyTargeting`) crashed the game when a party
  member joined.
- `std::from_chars` calls become `ytcoop::from_chars` (`ytcoop_compat.h`): Apple's libc++ has
  `from_chars` for `float` and `double` only from macOS 26, and the runtime targets macOS 15.
- `network/http.cpp` logs a blocking wait of the game's thread for a game server reply over
  100 ms ("the game waited ... ms"), to find frame freezes online.
- `bbport/gpu/shadps4/common/thread.cpp` spins with the Arm `yield` hint instead of x86 `pause`.
- The server link's UDP transport (`network/p2p_sockets.cpp`, `sockets.h`) runs on macOS as on
  Linux; its readiness `eventfd` comes from epoll-shim. Yharnam Together builds it for Linux and
  Windows only, and without it the game never links to the server.

Outside `vendor/`: `scripts/ytcoop/party_boss_events.py` is Yharnam Together's event patcher
(`tools/re/party_boss_events.py` of the same commit), changed only to run on Python 3.9
(annotations as strings, the manifest written as bytes).

The module's revision (`BB_SCM_REV`) is the vendored commit, as in the Windows build, so the
room's peers see the same build.

What this runtime puts in place of the Windows port's parts:

| Windows port | Here |
| --- | --- |
| `bbcoop/guest_hooks.cpp` (x86 trampolines) | `ytcoop_hooks.cpp`, through libbbgpu's int3 hooks (`bbgpu_guest_hook`), which bbcpu runs |
| `bbnet_glue.cpp` | `ytcoop_glue.cpp` (the bbnet ABI 2 of `gpu/bbnet.h`) |
| its overlay's team screen | `gpu/shim/bbport_ytcoop_ui.cpp` (a plainer team screen, loading rows, travel question) |
| the pad, sound, file and trophy calls | `src/runtime_coop.c` (trophies from the party are logged only: they stay local) |
| its launcher's co-op event files (`event-patch`, in `CUSA03173-mods`) | `scripts/ytcoop/events.py`, from `run.sh`: the same patcher's files made from the player's game into `<data>/ytcoop/event-patch` (again when the game or the patcher changes), laid over the game and mods (`mods.py --layer`); every player's map events then match |
| the launcher's Co-op page | the launcher's Online page, Yharnam Together (host address only; the player id is made once) |

Not ported yet: the party HUD (portraits, compass, pings), the map screen, duels, trading,
invasions and Discord status. Their code is in the module and runs; only their screens are
missing.

## Updating to a new release

The host refuses a game of another release ("Hello"'s version, `coop/build_fingerprint.h`), so
the module has to follow the friends' release exactly.

1. Check out the new tag in a Yharnam Together clone, apply `vendor-changes.diff`.
2. Copy the same file list into `vendor/` (add what the build now asks for).
3. Set `YT_RELEASE` in `CMakeLists.txt` to the new version.
4. Check `bbcoop.h` for changed functions the runtime calls (`src/runtime_coop.c`,
   `gpu/shim/bbport_ytcoop_ui.cpp`).

## Playing

Everyone needs game version 1.09 and the launcher's Online page set to Yharnam Together, with one
of its two servers:

- **Global**: Yharnam Together's public server (seamlesshunt.com). The launcher signs in with that
  server's own account (a name and a password sent to it alone, over HTTPS; the launcher keeps the
  30-day sign-in, never the password), asks its directory which shard to join, and pins the
  server's key (`BB_SHADNET_SERVER_KEY`), as Yharnam Together's launcher does.
- **A host's server**: one player runs Yharnam Together's server from its Windows release; the
  others reach it on TCP 31313, 31315 and 20443 and UDP 31314, and type its address.
