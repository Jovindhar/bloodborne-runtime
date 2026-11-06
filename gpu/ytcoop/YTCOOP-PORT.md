# Yharnam Together on BloodOnline (gpu/ytcoop)

BloodOnline's third online mode is Yharnam Together, the Bloodborne seamless co-op by lKaniel,
AMANISES and Supermedo (<https://github.com/Amanises-Yharnam-Together/BBSC-Yharnam-Together>,
GPL-2.0-or-later). Its own release is a Windows build of bbport; this directory builds its
co-op code as this runtime's online module, `libytcoop.dylib`, loaded instead of `libbbnet20`
when the launcher's Online page is set to Yharnam Together (`BB_NET_BACKEND=ytcoop`, run.sh).

## What is vendored

`vendor/` holds the files of Yharnam Together **v1.4.0** (tag `v1.4.0`, commit `efa0ad2`) that the
module compiles or includes, at their paths in that repository, with its LICENSE files and
README. They were picked from the build's dependency list, not by hand. The only change is in
`vendor-changes.diff`: `bbport/gpu/bbcoop/memory.cpp` gains a macOS `HostRegion`
(`mach_vm_region`), where the Windows build used `VirtualQuery` and Linux `/proc/self/maps`.

What this runtime puts in place of the Windows port's parts:

| Windows port | Here |
| --- | --- |
| `bbcoop/guest_hooks.cpp` (x86 trampolines) | `ytcoop_hooks.cpp`, through libbbgpu's int3 hooks (`bbgpu_guest_hook`), which bbcpu runs |
| `bbnet_glue.cpp` | `ytcoop_glue.cpp` (the bbnet ABI 2 of `gpu/bbnet.h`) |
| its overlay's team screen | `gpu/shim/bbport_ytcoop_ui.cpp` (a plainer team screen, loading rows, travel question) |
| the pad, sound, file and trophy calls | `src/runtime_coop.c` (trophies from the party are logged only: they stay local) |
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
