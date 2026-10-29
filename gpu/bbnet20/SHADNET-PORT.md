# The online module (libbbnet20)

## shadPS4's network code (`vendor/shadps4/`)

shadPS4's own sources at v.0.20.0
(https://github.com/shadps4-emu/shadPS4/tree/529ceb32f5546b820588f194b903cc39d08015fe):
`src/core/libraries/net`, `network`, `np` (trophies, commerce and the profile and Facebook
dialogs are not built) and `src/core/net` and `src/shadnet`. GPL-2.0-or-later, with their original
notices. They are unchanged except for `vendor-changes.diff`, every line of it marked `bbport`:

- The shadNet session starts once the game is loaded (`NpManager::InitializeRuntime`, called by
  `bbnet_start`): the account and the game's SDK version are known only then.
- The client reports itself to the server as this port (`BB_CLIENT_VERSION`), not as shadPS4.
- `sceHttp`, for The Hunter's Dream pool only: host overrides accept `*.example.com` keys, and the
  game's requests carry the account's token and ruleset headers.

The renderer, shaders and upscalers are not here. `bbnet_glue.cpp`, `include/` and `compat/`
implement what these libraries need from the rest of shadPS4 (kernel threads, the user service,
settings, the memory map) on this project's runtime. Online play is off unless `BB_ONLINE=1`.

BloodOnline (this fork): BBSC, Bloodborne seamless co-op by CLS_AMG
(https://github.com/Classmg22/BBSC, GPL-2.0-or-later), is part of this module as bbport 0.6 carried
it: `bbsc/` (its hooks, session persistence, cinematic camera and messages), `include/core/` and
`include/common/memory_patcher.h` (what it needs of shadPS4), and its additions to the network
code, outside `vendor-changes.diff`: the summon route, host placement and connect-first summons in
`network/http.cpp`, room-member replay in `np_matching2`, and signaling and P2P transport changes in
`np_signaling`, `signaling_handler.cpp` and `net/p2p_transport.cpp`. It runs only with `BB_BBSC=1`
(the launcher's seamless co-op switch); the official bloodborne-native builds leave it out because
it is built on the game's internals.

## The Hunter's Dream backend (`thd/`)

The co-op pool for The Hunter's Dream (`online_pool=huntersdream`, which sets
`BB_NET_BACKEND=thd`). It adapts droogie/bbhost's network side
(https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771):
`src/hle/net.cpp`, `src/hle/np_matching2.cpp`, `src/hle/np_signaling.cpp`, the NP parts of
`src/hle/system.cpp`, and `src/net/{http,account,session,stun}.cpp`. These portions are
GPL-3.0-or-later, copyright with the bbhost contributors, so the module as a whole is
GPL-3.0-or-later. With that pool the game's NP and sceNet imports are these, ahead of shadPS4's;
the shadNet pool does not use them.

What it touches in the game (`thd_session.cpp`), all at addresses of the 1.09 executable and only
after a 1.09 function's bytes were compared: one byte, the gate the game's matching callbacks
check before queuing events (on a console PSN sets it), and, read only, the host's area, level and
position, sent with an invite so the summoned player arrives next to the host.
