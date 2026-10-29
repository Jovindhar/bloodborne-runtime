// SPDX-License-Identifier: GPL-2.0-or-later
// bbport 0.6's hooks in the game's code (bbport_game_hooks.h). Taken from bbport 0.6's
// bbport_guest_hooks.cpp (8185b88, deadinside28/bloodborne_pc); its x86 detours are written as
// Core::InstallGuestCodeHook observers here, and host code calls the game's functions through
// runtime_guest_call (bbcpu on macOS).
#include "bbport_game_hooks.h"
#include "bbport_effect_flags.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_settings.h"
#include "bloodborne_menu_pointer.h"
#include "cpu/bbcpu.h"
#include "platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

#include "common/types.h"

extern "C" {
int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
int runtime_extra_direct_map(uint64_t size, uint64_t alignment, void** out);
uint64_t runtime_guest_call(const void* fn, int count, const uint64_t* args);
}

namespace BbGameHooks {
namespace {
using u64 = std::uint64_t;
using u8 = std::uint8_t;

u64 image_base = 0;

u64 Word(u64 address) {
    return runtime_memory_is_mapped(address, 8) ? *reinterpret_cast<const u64*>(address) : 0;
}

bool Hook(u64 offset, std::span<const u8> code, u64 tag, Core::GuestCodeHook fn) {
    return Core::InstallGuestCodeHook(reinterpret_cast<void*>(image_base + offset), code, tag, fn);
}

// ---- Laurence's cinematic above 30 FPS ----

// The game's render jobs take a slot from a pool and give it back when they end. A job that fails
// (as many do at high frame rates in Laurence's cinematic) kept its slot: the pool ran dry and
// the cinematic hung. The slot now goes back at the end of the failed run (0x1068092), and when a
// runner is destroyed or reset with one still held (0x106798a, 0x1067a5c).
std::atomic<u64> job_slots_released{0};
constexpr u64 GiveBackSlot = 0x1056990; ///< (pool, item)
constexpr u64 SlotPool = 0x54b8788;     ///< the pool's global

void GiveBack(u64 pool, u64 runner) {
    u64& held = *reinterpret_cast<u64*>(runner + 0x120);
    const u64 item = held;
    if (!item || !pool) {
        return;
    }
    const u64 args[2] = {pool, item};
    runtime_guest_call(reinterpret_cast<const void*>(image_base + GiveBackSlot), 2, args);
    held = 0;
}

void PS4_SYSV_ABI JobSlotFix(u64, const Core::GuestRegisterSnapshot* r) {
    // r14 the runner, r12 the pool, r15d the run's result (1: failed), eax the error.
    if (std::uint32_t(r->r15) != 1 || !Word(r->r14 + 0x120)) {
        return;
    }
    GiveBack(r->r12, r->r14);
    const u64 n = job_slots_released.fetch_add(1) + 1;
    if (n <= 5 || (n & (n - 1)) == 0) {
        std::printf("Game hooks: a failed render job gave its slot back (%llu so far); error=%u\n",
                    static_cast<unsigned long long>(n), unsigned(std::uint32_t(r->rax)));
    }
}

void PS4_SYSV_ABI JobSlotDrop(u64, const Core::GuestRegisterSnapshot* r) {
    // rbx the runner, about to clear its held slot.
    if (!Word(r->rbx + 0x120)) {
        return;
    }
    GiveBack(Word(image_base + SlotPool), r->rbx);
    const u64 n = job_slots_released.fetch_add(1) + 1;
    if (n <= 5 || (n & (n - 1)) == 0) {
        std::printf("Game hooks: a render job destroyed with its slot gave it back (%llu so far)\n",
                    static_cast<unsigned long long>(n));
    }
}

bool InstallJobSlotFix() {
    if (const char* env = std::getenv("BB_JOB_SLOT_FIX"); env && env[0] == '0') {
        return false;
    }
    // mov rax, [rbx]; cmp rax, [rbp - 0x30]: the stack check every exit of that function ends in.
    static constexpr std::array<u8, 7> exit_code = {0x48, 0x8b, 0x03, 0x48, 0x3b, 0x45, 0xd0};
    // mov qword [rbx + 0x120], 0
    static constexpr std::array<u8, 11> drop_code = {0x48, 0xc7, 0x83, 0x20, 0x01, 0x00,
                                                     0x00, 0x00, 0x00, 0x00, 0x00};
    return Hook(0x1068092, exit_code, 0, JobSlotFix) & Hook(0x106798a, drop_code, 0, JobSlotDrop) &
           Hook(0x1067a5c, drop_code, 1, JobSlotDrop);
}

// The render resource cache (its range heap set up right after the allocation at 0x216b21d) has
// 160 MiB of its own. At high frame rates it fills before the cinematic's geometry is prepared;
// failed allocations then leave models exploded or missing. 320 MiB still failed on 4 MiB requests
// in the same load; 512 MiB survived that peak. Taken from the game's own allocator the larger
// backing starved what areas load into, so the game allocates its 160 MiB as always and the cache
// gets the port's direct memory past the game's size instead (runtime_extra_direct_map,
// BB_DMEM_EXTRA_MB). BB_RENDER_RESOURCE_CACHE_MB=160: the game's cache as it is.
void PS4_SYSV_ABI ResourceCacheBacking(u64, const Core::GuestRegisterSnapshot* r) {
    constexpr u64 native_bytes = 160ull << 20;
    std::uint32_t requested = 0;
    std::memcpy(&requested, reinterpret_cast<const void*>(r->r14 + 0x54), sizeof(requested));
    if (requested != native_bytes || !r->rax) {
        return;
    }
    static const std::uint32_t bytes = [] {
        const char* env = std::getenv("BB_RENDER_RESOURCE_CACHE_MB");
        const long mb = env ? std::strtol(env, nullptr, 10) : 512;
        return std::uint32_t(std::clamp(mb, 160l, 512l)) << 20;
    }();
    if (bytes == native_bytes) {
        return;
    }
    void* block = nullptr;
    if (const int error = runtime_extra_direct_map(bytes, 64ull << 10, &block); error || !block) {
        std::printf("Render resource cache: no port memory for %u MiB (error %#x): the game's "
                    "160 MiB\n", bytes >> 20, unsigned(error));
        return;
    }
    // The creation record can be a stack object: not necessarily in the runtime's map.
    std::memcpy(reinterpret_cast<void*>(r->r14 + 0x54), &bytes, sizeof(bytes));
    const_cast<Core::GuestRegisterSnapshot*>(r)->rax = reinterpret_cast<u64>(block);
    std::printf("Render resource cache: %u MiB in the port's memory at %p (the game's 160 MiB "
                "stays allocated)\n", bytes >> 20, block);
}

bool InstallResourceCacheSize() {
    // mov rcx, rbx; mov [rcx + 0x10], rax: right after the allocator's Allocate(size, 16) returns.
    static constexpr std::array<u8, 7> code{0x48, 0x89, 0xd9, 0x48, 0x89, 0x41, 0x10};
    return Hook(0x216b21d, code, 0, ResourceCacheBacking);
}
// ---- Effects switched while the game runs ----
// Ambient occlusion (SSAO), motion blur, the game's own anti-aliasing and the shadows of dynamic
// lights are bytes of the renderer's options (+0x2968, +0x2969, +0x296a, +0x296e; the patches
// change the immediates that set them in the constructor). The renderer tests them while it builds
// each frame. The constructor's tail (0x22c254c) reports the options' address; they are written
// there and on every change. Chromatic aberration (0x229faa8), depth of field (0x21d7bbc) and
// bloom (0x21d72f0) are code the patches replace: written back and forth here; the game's own
// bytes are kept as they were found. Screen-space reflections stay a patch at start (it also sizes a target in the
// constructor). BB_HOT_EFFECTS=0: the patches at start, as before (patches.py).
constexpr std::size_t MaxEffectObjects = 64;
std::array<std::atomic<u64>, MaxEffectObjects> effects_objects{};
std::atomic<std::size_t> effects_object_count{0};
std::array<std::atomic<int>, BbSettings::EffectCount> effects_applied{};
std::atomic<bool> hot_effects{false};
struct HotCode {
    int effect;               ///< BbSettings::Effects index
    u64 offset;
    std::array<u8, 12> patch; ///< the community patch's bytes (the effect off)
    std::size_t size;
    std::array<u8, 12> original{};
    bool ready = false;
};
std::array<HotCode, 3> hot_code = {{
    {0, 0x229faa8, {0xc7, 0x83, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90}, 12},
    {1, 0x21d7bbc, {0xe9, 0xdf, 0x00, 0x00, 0x00, 0x90}, 6},
    {7, 0x21d72f0, {0x31, 0xf6, 0x0f, 0x1f, 0x40, 0x00}, 6}, // bloom ("Disable Bloom", Kyo)
}};
struct HotFlag {
    int effect;
    std::uint32_t offset;
};
constexpr std::array<HotFlag, 4> hot_flags = {{{2, 0x2969}, {3, 0x2968}, {4, 0x296a}, {5, 0x296e}}};

/// Writes `size` bytes of code from the last to the first (the first byte, an opcode, is the one
/// that makes the new code reachable); bbcpu drops what it translated from the old bytes.
void WriteHotCode(u64 address, const u8* bytes, std::size_t size) {
    const long page_size = sysconf(_SC_PAGESIZE);
    void* page = reinterpret_cast<void*>(address & ~u64(page_size - 1));
    const std::size_t span = (address + size) - reinterpret_cast<u64>(page);
#if defined(__APPLE__)
    mprotect(page, span, PROT_READ | PROT_WRITE);
#else
    mprotect(page, span, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    auto* p = reinterpret_cast<u8*>(address);
    for (std::size_t i = size; i-- > 1;) {
        __atomic_store_n(p + i, bytes[i], __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(p, bytes[0], __ATOMIC_SEQ_CST);
    mprotect(page, span, Core::GuestCodeProtection());
    if (bbcpu_enabled()) {
        bbcpu_invalidate(address, size);
    }
}

void ApplyHotEffects(bool force, u64 fresh = 0) {
    auto& settings = BbSettings::Get();
    for (auto& code : hot_code) {
        if (!code.ready) {
            continue;
        }
        const int want = settings.effects[code.effect].load(std::memory_order_relaxed) ? 1 : 0;
        // Code already in that state is not rewritten, forced or not: each write drops bbcpu's
        // translations, which stalled the main thread whenever the game built a renderer (the
        // status page).
        if (effects_applied[code.effect].load() == want) {
            continue;
        }
        effects_applied[code.effect].store(want);
        WriteHotCode(image_base + code.offset, want ? code.original.data() : code.patch.data(), code.size);
    }
    const std::size_t count = std::min(effects_object_count.load(std::memory_order_acquire), MaxEffectObjects);
    std::size_t live_objects = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const u64 object = effects_objects[i].load(std::memory_order_relaxed);
        const bool is_fresh = object == fresh; // just built: certainly an options object
        // The game frees these objects and reuses the memory: written only while its switches
        // (+0x2968..+0x2970) still look like switches and the constants the constructor copies
        // from the game's data (32 bytes at +0x2980) are still whole. A block of zeros once
        // passed for switches and a write there damaged the game's heap.
        u8 block[0x40]{};
        if (!runtime_memory_is_mapped(object + 0x2960, sizeof(block))) {
            continue;
        }
        std::memcpy(block, reinterpret_cast<const void*>(object + 0x2960), sizeof(block));
        bool plausible =
            std::memcmp(block + 0x20, reinterpret_cast<const void*>(image_base + 0x5128a40), 32) == 0;
        for (int b = 8; b <= 0x10; ++b) {
            plausible &= block[b] <= 1;
        }
        if (!plausible && !is_fresh) {
            continue;
        }
        ++live_objects;
        for (const auto& flag : hot_flags) {
            int want = settings.effects[flag.effect].load(std::memory_order_relaxed) ? 1 : 0;
            if (flag.effect == 3) {
                // Menus and auxiliary contexts can have no SSAO target: the game's own choice is
                // kept (a forced SSAO there crashed after selecting an item).
                want = BbEffectFlags::SsaoEnabled({reinterpret_cast<const u8*>(object), 0x29a0}, want != 0);
            } else if (flag.effect == 5) {
                // A secondary renderer attaches its owner at +0x1b90 and clears dynamic shadows.
                // Re-enabling them there leaves its worker without a shadow context.
                want &= *reinterpret_cast<const u64*>(object + 0x1b90) == 0;
            }
            auto* byte = reinterpret_cast<volatile u8*>(object + flag.offset);
            if (force || *byte != want) {
                __atomic_store_n(const_cast<u8*>(byte), u8(want), __ATOMIC_RELAXED);
            }
        }
    }
    static std::atomic<int> told{0};
    if (force && told.fetch_add(1) < 8) {
        std::printf("Game hooks: effects written to %zu of %zu renderer options objects (the rest "
                    "are gone)\n", live_objects, count);
    }
}

/// The renderer options object is built (rbx at the constructor's tail): its switches follow the
/// settings from the start.
void PS4_SYSV_ABI EffectsConstructed(u64, const Core::GuestRegisterSnapshot* r) {
    const u64 object = r->rbx;
    const std::size_t known = std::min(effects_object_count.load(std::memory_order_acquire), MaxEffectObjects);
    bool seen = false;
    for (std::size_t i = 0; i < known; ++i) {
        seen |= effects_objects[i].load(std::memory_order_relaxed) == object;
    }
    if (!seen) {
        const std::size_t at = effects_object_count.fetch_add(1);
        if (at < MaxEffectObjects) {
            effects_objects[at].store(object, std::memory_order_release);
        } else {
            effects_object_count.store(MaxEffectObjects);
        }
    }
    ApplyHotEffects(true, object);
}

bool InstallHotEffects() {
    if (const char* env = std::getenv("BB_HOT_EFFECTS"); env && env[0] == '0') {
        return false;
    }
    // mov qword [rbx + 0x2977], 0
    static constexpr std::array<u8, 11> tail = {0x48, 0xc7, 0x83, 0x77, 0x29, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00};
    // The code effects: the game's own bytes, taken before anything is written.
    for (auto& code : hot_code) {
        const u8* at = reinterpret_cast<const u8*>(image_base + code.offset);
        if (std::memcmp(at, code.patch.data(), code.size) == 0) {
            continue; // the startup patch is in: left as it is
        }
        std::memcpy(code.original.data(), at, code.size);
        code.ready = true;
    }
    if (!Hook(0x22c254c, tail, 0, EffectsConstructed)) {
        return false;
    }
    for (auto& code : hot_code) {
        if (code.ready) {
            effects_applied[code.effect].store(-1);
#ifdef BB_HAVE_BBCPU
            // Rewritten while the game runs: never taken from the recompiled library.
            bbcpu_recomp_exclude(image_base + code.offset, code.size);
#endif
        }
    }
    ApplyHotEffects(true); // the code effects before the game runs
    hot_effects = true;
    return true;
}
// ---- The output's aspect ratio (bbport 0.6) ----
// Data the redirected instructions read: within a rel32 of the game's code (the runtime's low
// memory, after the image).
struct alignas(16) MarkerScale {
    float game[4];                     ///< the game's constant
    std::atomic<std::uint32_t> now[4]; ///< what the instruction multiplies by
};
struct NearData {
    std::array<MarkerScale, 3> marker_scales;
};
NearData* near_data = nullptr;

// The camera manager copies the active camera's projection parameters into the current camera
// every frame (+0x50 vertical field of view, +0x54 aspect ratio, +0x58 near, +0x5c far; four plain
// copies, and a blend of two cameras, one of them per update); the projection, the culling and
// the HUD's markers read them there. After the copy the output's aspect ratio replaces the game's
// 1.777 when one is set: a wider output sees more to the sides at the same height; a taller one
// keeps the width and sees more above and below (the field of view widened: 2 atan(tan(fov / 2)
// * 16:9 / aspect)).
std::atomic<std::uint32_t> camera_aspect_bits{0}; ///< float bits; 0: the game's
std::atomic<std::uint32_t> camera_fov_scale{0};   ///< float bits: the tangent's factor; 0: none
std::atomic<u64> camera_state{0};                 ///< the current camera
struct ParamStore {
    u64 offset;
    std::array<u8, 5> code; ///< the store the hook follows
};
constexpr std::array<ParamStore, 5> CameraParamStores = {{
    {0x1436c2c, {0xc5, 0xfa, 0x11, 0x43, 0x5c}}, // vmovss %xmm0, 0x5c(%rbx): the follow camera
    {0x1436d8a, {0xc5, 0xfa, 0x11, 0x43, 0x5c}},
    {0x1436e68, {0xc5, 0xf8, 0x11, 0x4b, 0x50}}, // vmovups %xmm1, 0x50(%rbx): a blend of two
    {0x1436eec, {0xc5, 0xfa, 0x11, 0x43, 0x5c}},
    {0x1436f4b, {0xc5, 0xfa, 0x11, 0x43, 0x5c}},
}};

void PS4_SYSV_ABI CameraParams(u64, const Core::GuestRegisterSnapshot* r) {
    auto* camera = reinterpret_cast<float*>(r->rbx);
    camera_state.store(r->rbx, std::memory_order_relaxed);
    if (const std::uint32_t bits = camera_aspect_bits.load(std::memory_order_relaxed)) {
        std::memcpy(&camera[0x54 / 4], &bits, 4);
    }
    if (const std::uint32_t bits = camera_fov_scale.load(std::memory_order_relaxed)) {
        float factor;
        std::memcpy(&factor, &bits, 4);
        camera[0x50 / 4] = 2.0f * std::atan(std::tan(camera[0x50 / 4] * 0.5f) * factor);
    }
}

int InstallCameraAspectHooks() {
    int installed = 0;
    for (const auto& site : CameraParamStores) {
        installed += Core::InstallGuestCodeHookAfter(reinterpret_cast<void*>(image_base + site.offset),
                                                     site.code, 0, CameraParams);
    }
    return installed;
}

// The HUD's markers over characters (lock-on, enemies' health bars, damage numbers): projected
// with the current camera, then placed in the game's 1920x1080 UI by `* (0.5, -0.5, ..) +
// (0.5, 0.5, ..)` (three places). The UI is drawn in the middle of a wider (taller) output at its
// height (width): there a marker's 0.5 becomes 0.5 * output aspect / 16:9 (16:9 / aspect for y).
// The multiply reads the runtime's vector instead of the game's constant (its operand moved).
struct MarkerSite {
    u64 offset;
    u64 scale; ///< the constant the multiply reads
};
constexpr std::array<MarkerSite, 3> MarkerSites = {{
    {0x1644379, 0x48f9a00},
    {0x1644c45, 0x48f9a60},
    {0x16452b7, 0x48f9ad0},
}};
std::atomic<int> marker_sites{0};
std::atomic<float> marker_kx{1.0f}, marker_ky{1.0f};

void UpdateMarkerScales() {
    if (!near_data) return;
    const float kx = marker_kx.load(std::memory_order_relaxed), ky = marker_ky.load(std::memory_order_relaxed);
    for (MarkerScale& m : near_data->marker_scales) {
        const float now[4] = {m.game[0] * kx, m.game[1] * ky, m.game[2], m.game[3]};
        for (int c = 0; c < 4; ++c) {
            std::uint32_t v;
            std::memcpy(&v, &now[c], 4);
            m.now[c].store(v, std::memory_order_relaxed);
        }
    }
}

int InstallMarkerHooks() {
    if (!near_data) return 0;
    int installed = 0;
    for (std::size_t i = 0; i < MarkerSites.size(); ++i) {
        const MarkerSite& site = MarkerSites[i];
        // vmulps disp(%rip), %xmm0, %xmm0 reading `scale`.
        auto* code = reinterpret_cast<u8*>(image_base + site.offset);
        std::int32_t disp;
        std::memcpy(&disp, code + 4, 4);
        if (code[0] != 0xc5 || code[1] != 0xf8 || code[2] != 0x59 || code[3] != 0x05 ||
            std::int64_t(site.offset) + 8 + disp != std::int64_t(site.scale)) {
            continue;
        }
        MarkerScale& m = near_data->marker_scales[i];
        std::memcpy(m.game, reinterpret_cast<const void*>(image_base + site.scale), sizeof(m.game));
        const std::int64_t to = std::int64_t(reinterpret_cast<u64>(&m.now)) -
                                std::int64_t(image_base + site.offset + 8);
        if (to != std::int32_t(to)) continue;
        const std::int32_t to32 = std::int32_t(to);
        WriteHotCode(reinterpret_cast<u64>(code + 4), reinterpret_cast<const u8*>(&to32), 4);
        ++installed;
    }
    UpdateMarkerScales();
    marker_sites.store(installed, std::memory_order_relaxed);
    return installed;
}

// The game's UI (Scaleform movies). The menu system sets each movie's viewport every frame
// (SetViewport, 0x1f510: rsi a GFx::Viewport: buffer 1920x1080, ShowAll, centred). With the
// output's pixel aspect ratio there (Viewport::AspectRatio: (output width / height) / 16:9), a
// movie shows its 16:9 screen in the middle of a wider (taller) view, squeezed into the 1920x1080
// frame, which the output stretches back (UiComposition::Scale): what lies past the game's screen
// (the HUD's groups at the edges) is drawn, not culled.
std::atomic<float> gfx_pixel_aspect{1.0f};
void PS4_SYSV_ABI ViewportAdjust(u64, const Core::GuestRegisterSnapshot* r) {
    auto* v = reinterpret_cast<std::int32_t*>(r->rsi);
    // The game's full-screen movies (1920x1080 in a 1920x1080 frame) only.
    if (v[0] == 1920 && v[1] == 1080 && v[2] == 0 && v[3] == 0 && v[4] == 1920 && v[5] == 1080) {
        const float aspect = gfx_pixel_aspect.load(std::memory_order_relaxed);
        std::memcpy(v + 12, &aspect, 4);
    }
}

// The HUD anchored to the output's edges. The game lays its HUD out for 1920x1080; on a wider
// output the UI shows that screen in the middle, and its groups would stay there. They are moved
// as the game moves its own clips: through the movie's members (GFx::Value, SetMember "x"/"y")
// from the game's screen update (0x1bf9e10, rdi the screen; the HUD a value wrapper at +0xa80,
// its movie's root GFx::Value at +0x30). The bars, items and gauges (PlayerHUD) go to the left
// edge, blood echoes and insight to the right, the autosave icon right; on a taller output the
// top ones go up and the boss bar down. BB_HUD_ANCHOR=0 keeps them in the 16:9 middle.
struct GfxValue {
    u64 links[2] = {~u64(0), ~u64(0)};
    void* object_interface = nullptr;
    std::uint32_t type = 0, pad = 0;
    union {
        double number;
        void* data;
    } value{};
    u64 aux = 0;
};
static_assert(sizeof(GfxValue) == 0x30);
constexpr std::uint32_t GfxManaged = 0x40, GfxNumber = 5, GfxDisplayObject = 10;
constexpr std::size_t SlotRelease = 0x18, SlotGetMember = 0x28, SlotSetMember = 0x30;
u64 GfxMethod(void* oi, std::size_t slot) {
    return *reinterpret_cast<const u64*>(*reinterpret_cast<u8**>(oi) + slot);
}
u64 Call(u64 fn, std::initializer_list<u64> args) {
    return runtime_guest_call(reinterpret_cast<const void*>(fn), int(args.size()), args.begin());
}
bool GfxIsObject(const GfxValue& v) {
    return v.object_interface && (v.type & 0x8c) == 8;
}
void GfxRelease(GfxValue& v) {
    if (v.object_interface && (v.type & GfxManaged)) {
        Call(GfxMethod(v.object_interface, SlotRelease),
             {u64(v.object_interface), u64(&v), u64(v.value.data)});
    }
    v = GfxValue{};
}
bool GfxGet(const GfxValue& object, const char* name, GfxValue& out) {
    out = GfxValue{};
    if (!GfxIsObject(object)) return false;
    const bool isdobj = (object.type & 0x8f) == GfxDisplayObject;
    return (Call(GfxMethod(object.object_interface, SlotGetMember),
                 {u64(object.object_interface), u64(object.value.data), u64(name), u64(&out), u64(isdobj)}) &
            0xff) != 0;
}
bool GfxGetNumber(const GfxValue& object, const char* name, double& number) {
    GfxValue v;
    const bool ok = GfxGet(object, name, v) && (v.type & 0x8f) == GfxNumber;
    if (ok) number = v.value.number;
    GfxRelease(v);
    return ok;
}
bool GfxSetNumber(const GfxValue& object, const char* name, double number) {
    if (!GfxIsObject(object)) return false;
    GfxValue v;
    v.type = GfxNumber;
    v.value.number = number;
    const bool isdobj = (object.type & 0x8f) == GfxDisplayObject;
    return (Call(GfxMethod(object.object_interface, SlotSetMember),
                 {u64(object.object_interface), u64(object.value.data), u64(name), u64(&v), u64(isdobj)}) &
            0xff) != 0;
}

constexpr u64 HudWrapperVtable = 0x539a070; ///< the value wrapper's
std::atomic<float> hud_dx{0.0f}, hud_dy{0.0f}; ///< the shift for an edge, in the HUD's pixels
bool hud_anchor_on = true;
struct HudItem {
    int parent; ///< 0 the root, 1 PlayerHUD
    const char* name;
    float fx, fy;
    double base_x = 0, base_y = 0;
    double set_x = 0, set_y = 0; ///< what the runtime set last
    bool known = false;
};
std::array<HudItem, 5> hud_items = {{
    {0, "PlayerHUD", -1.0f, -1.0f},
    {1, "Soul", 2.0f, 0.0f}, // in PlayerHUD: twice, for its move to the left
    {1, "HeroPoint", 2.0f, 0.0f},
    {0, "AutoSaveIcon", 1.0f, -1.0f},
    {0, "BossTop", 0.0f, 1.0f},
}};

void PS4_SYSV_ABI HudFrame(u64, const Core::GuestRegisterSnapshot* r) {
    const u64 screen = r->rdi;
    const u64 hud = screen ? *reinterpret_cast<const u64*>(screen + 0xa80) : 0;
    if (!hud || *reinterpret_cast<const u64*>(hud) != image_base + HudWrapperVtable) {
        return;
    }
    const GfxValue& root = *reinterpret_cast<const GfxValue*>(hud + 0x30);
    static u64 last_hud = 0;
    static unsigned frames = 0;
    static float applied_dx = 0.0f, applied_dy = 0.0f;
    if (hud != last_hud) {
        last_hud = hud; // a new HUD (another area): where the game put its groups is read again
        for (HudItem& item : hud_items) item.known = false;
    }
    const float dx = hud_anchor_on ? hud_dx.load(std::memory_order_relaxed) : 0.0f;
    const float dy = hud_anchor_on ? hud_dy.load(std::memory_order_relaxed) : 0.0f;
    if (dx == 0.0f && dy == 0.0f && applied_dx == 0.0f && applied_dy == 0.0f) {
        return; // 16:9: nothing to move
    }
    bool all_known = true;
    for (const HudItem& item : hud_items) all_known = all_known && item.known;
    // Rechecked every half second: the game may put a group back itself.
    if (dx == applied_dx && dy == applied_dy && all_known && ++frames % 32 != 0) {
        return;
    }
    if (!GfxIsObject(root)) {
        return;
    }
    GfxValue player;
    GfxGet(root, "PlayerHUD", player);
    for (HudItem& item : hud_items) {
        GfxValue clip;
        if (!GfxGet(item.parent ? player : root, item.name, clip) || !GfxIsObject(clip)) {
            GfxRelease(clip);
            continue;
        }
        double x = 0, y = 0;
        if (GfxGetNumber(clip, "x", x) && GfxGetNumber(clip, "y", y)) {
            // Positions are in twips (1/20 pixel): a read back is that close.
            const auto moved = [](double a, double b) { return std::abs(a - b) > 0.06; };
            if (!item.known || moved(x, item.set_x) || moved(y, item.set_y)) {
                item.base_x = x; // first seen, or moved by the game since
                item.base_y = y;
                item.known = true;
            }
            const double nx = item.base_x + item.fx * dx, ny = item.base_y + item.fy * dy;
            if (moved(nx, x)) GfxSetNumber(clip, "x", nx);
            if (moved(ny, y)) GfxSetNumber(clip, "y", ny);
            double check_x = nx, check_y = ny;
            GfxGetNumber(clip, "x", check_x);
            GfxGetNumber(clip, "y", check_y);
            item.set_x = check_x;
            item.set_y = check_y;
        }
        GfxRelease(clip);
    }
    GfxRelease(player);
    applied_dx = dx;
    applied_dy = dy;
}

bool InstallUiHooks() {
    // Both functions start with push rbp; mov rbp, rsp; push r15.
    static constexpr std::array<u8, 6> entry = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    if (const char* env = std::getenv("BB_HUD_ANCHOR"); env && env[0] == '0') hud_anchor_on = false;
    return Hook(0x1f510, entry, 0, ViewportAdjust) & Hook(0x1bf9e10, entry, 0, HudFrame);
}

// Yebis's blur-kernel sample parameters (0xbbc3e0): the base sample count (edi) must be at least
// 3 (an assertion the retail build has compiled out). Game data now and then gives 2; the function
// then runs on in a state that does not end (the guest stops submitting: a black screen), so the
// count is raised to 3 at the entry. BB_KERNEL_CLAMP=0: not installed.
void PS4_SYSV_ABI KernelSampleClamp(u64, const Core::GuestRegisterSnapshot* r) {
    if (std::int32_t(r->rdi) < 3) {
        const_cast<Core::GuestRegisterSnapshot*>(r)->rdi = (r->rdi & ~u64(0xffffffff)) | 3;
    }
}

bool InstallKernelSampleClamp() {
    if (const char* env = std::getenv("BB_KERNEL_CLAMP"); env && env[0] == '0') {
        return false;
    }
    static constexpr std::array<u8, 6> entry = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    return Hook(0xbbc3e0, entry, 0, KernelSampleClamp);
}
// ---- Native mouse look (bbport 0.6) ----
// The follow camera's update (0x143ac60, rdi the camera) derives its angles from where the camera
// is around the player each frame, pitch at +0x140 and yaw at +0x144; in its normal mode
// 0x143cde0..0x143cdf0 makes xmm1 = camera stick * turn speed * frame time, the increment the game
// adds to (pitch, yaw) before it wraps the yaw and clamps the pitch. After those instructions the
// mouse's turn since the last update (radians, taken and cleared atomically) is added to it: the
// mouse turns the camera as far as it moved, with none of the stick's dead zone, response curve,
// acceleration or top speed. A turn counts as camera input, as the stick does (else the update
// pulls the pitch back to its rest angle at once).
constexpr std::array<u8, 16> LookSiteCode = {0xc5, 0xa8, 0x59, 0xc9,                         // vmulps xmm1, xmm10, xmm1
                                             0xc5, 0xf8, 0x28, 0x95, 0x10, 0xfc, 0xff, 0xff, // vmovaps xmm2, [rbp - 0x3f0]
                                             0xc5, 0xe8, 0x59, 0xc9};                        // vmulps xmm1, xmm2, xmm1
std::atomic<u64> look_delta{0}; ///< (pitch, yaw) as two floats, taken by the hook
std::atomic<bool> look_installed{false};
std::atomic<long long> look_since_ms{0}; ///< when look_delta last went from empty to not

void PS4_SYSV_ABI LookTurn(u64, const Core::GuestRegisterSnapshot* r) {
    const u64 delta = look_delta.exchange(0, std::memory_order_acq_rel);
    if (!delta) {
        return;
    }
    *reinterpret_cast<std::uint32_t*>(r->rbp - 0x3c4) = 1;
    *reinterpret_cast<u8*>(r->r13 + 0x15c) = 1;
    *reinterpret_cast<std::uint32_t*>(r->r13 + 0x154) = 0;
    float add[2], now[2];
    std::memcpy(add, &delta, 8);
    auto& xmm1 = const_cast<Core::GuestRegisterSnapshot*>(r)->ymm[1];
    std::memcpy(now, xmm1.data(), 8);
    now[0] += add[0];
    now[1] += add[1];
    std::memcpy(xmm1.data(), now, 8);
}

// The free camera (the community patch "Restore Debug Camera": hold Cross and press L3) keeps its
// mode in the camera manager (r12 at its check 0x1de344c): byte +0xc4 is 1 while it is on. Only
// installed where the patch is in place; the mouse then drives its right stick.
std::atomic<u64> free_camera_manager{0};
void PS4_SYSV_ABI FreeCameraProbe(u64, const Core::GuestRegisterSnapshot* r) {
    free_camera_manager.store(r->r12, std::memory_order_relaxed);
}

void InstallMouseHooks() {
    // BB_NATIVE_MOUSE=0: the mouse drives the right stick instead (runtime_pad.c).
    if (const char* env = std::getenv("BB_NATIVE_MOUSE"); !(env && env[0] == '0')) {
        look_installed = Core::InstallGuestCodeHookAfter(reinterpret_cast<void*>(image_base + 0x143cde0),
                                                         LookSiteCode, 0, LookTurn);
    }
    static constexpr std::array<u8, 9> free_camera = {0x41, 0x80, 0xbc, 0x24, 0xc4, 0x00, 0x00, 0x00, 0x01};
    const bool probe = std::memcmp(reinterpret_cast<const void*>(image_base + 0x1de344c), free_camera.data(),
                                   free_camera.size()) == 0 &&
                       Hook(0x1de344c, free_camera, 0, FreeCameraProbe);
    std::printf("Game hooks: native mouse look %s; free camera probe %s\n",
                look_installed ? "on (the mouse turns the camera itself)" : "off (the mouse drives the right stick)",
                probe ? "on" : "off (no Restore Debug Camera patch)");
}
} // namespace

void ApplyGameEffects() {
    if (hot_effects.load(std::memory_order_relaxed)) {
        ApplyHotEffects(false);
    }
}

bool HotEffects() {
    return hot_effects.load(std::memory_order_relaxed);
}

bool FreeCameraActive() {
    const u64 manager = free_camera_manager.load(std::memory_order_relaxed);
    return manager != 0 && *reinterpret_cast<const volatile u8*>(manager + 0xc4) == 1;
}

bool CameraLookAvailable() {
    return look_installed.load(std::memory_order_acquire);
}

bool CameraLookAdd(float pitch, float yaw) {
    if (!CameraLookAvailable()) {
        return false;
    }
    // Whether the camera takes the turns: a turn it has not taken for 100 ms (lock-on, menus,
    // cutscenes: its normal update does not run) means not, until it takes one again. Meanwhile
    // the caller drives the stick (lock-on switches targets with the stick) and the turn waiting
    // here is replaced rather than applied all at once later.
    static std::atomic<bool> active{true}, stored{false};
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    u64 old = look_delta.load(std::memory_order_acquire);
    if (stored.load(std::memory_order_relaxed)) {
        if (old == 0) {
            active.store(true, std::memory_order_relaxed);
        } else if (now - look_since_ms.load(std::memory_order_relaxed) > 100) {
            active.store(false, std::memory_order_relaxed);
        }
    }
    const bool taking = active.load(std::memory_order_relaxed);
    if (pitch == 0.0f && yaw == 0.0f) {
        return taking;
    }
    for (;;) {
        float p = 0.0f, y = 0.0f;
        if (old != 0 && taking) {
            const std::uint32_t lo = std::uint32_t(old), hi = std::uint32_t(old >> 32);
            std::memcpy(&p, &lo, 4);
            std::memcpy(&y, &hi, 4);
        }
        p += pitch;
        y += yaw;
        std::uint32_t lo, hi;
        std::memcpy(&lo, &p, 4);
        std::memcpy(&hi, &y, 4);
        const u64 next = u64(lo) | (u64(hi) << 32);
        if (look_delta.compare_exchange_weak(old, next, std::memory_order_acq_rel)) {
            if (old == 0 || !taking) {
                look_since_ms.store(now, std::memory_order_relaxed);
            }
            stored.store(true, std::memory_order_relaxed);
            return taking;
        }
    }
}

void SetDisplayAspect(float aspect) {
    constexpr float game = 16.0f / 9.0f;
    const bool other = aspect > 0.5f && aspect < 8.0f && std::abs(aspect - game) > 0.001f;
    std::uint32_t bits = 0, fov = 0;
    float kx = 1.0f, ky = 1.0f; // the markers' scale: the UI is as high (wide) as the output
    if (other) {
        std::memcpy(&bits, &aspect, 4);
        if (aspect < game) {
            ky = game / aspect;
            std::memcpy(&fov, &ky, 4);
        } else {
            kx = aspect / game;
        }
    }
    camera_aspect_bits.store(bits, std::memory_order_relaxed);
    camera_fov_scale.store(fov, std::memory_order_relaxed);
    // The HUD's groups: as far as the output's edge is from the 16:9 middle (the HUD's pixels).
    hud_dx.store(960.0f * (kx - 1.0f), std::memory_order_relaxed);
    hud_dy.store(540.0f * (ky - 1.0f), std::memory_order_relaxed);
    gfx_pixel_aspect.store(other ? aspect / game : 1.0f, std::memory_order_relaxed);
    marker_kx.store(kx, std::memory_order_relaxed);
    marker_ky.store(ky, std::memory_order_relaxed);
    UpdateMarkerScales();
    std::printf("Display: aspect ratio %.3f (%s)\n", double(aspect),
                other ? (aspect > game ? "wider than 16:9: the camera sees more to the sides"
                                       : "taller than 16:9: the camera sees more above and below")
                      : "the game's");
}

void PatchImage(unsigned char* image, std::uint64_t) {
    // BB_GAME_HOOKS=0 (comparisons): none of bbport 0.6's game hooks.
    if (const char* env = std::getenv("BB_GAME_HOOKS"); env && env[0] == '0') {
        std::printf("Game hooks: off (BB_GAME_HOOKS=0)\n");
        return;
    }
    image_base = reinterpret_cast<u64>(image);
    near_data = static_cast<NearData*>(bb_low_map(sizeof(NearData), PROT_READ | PROT_WRITE));
    if (near_data) new (near_data) NearData{};
    std::printf("Game hooks: render job slots %s; render resource cache %s; effects %s; blur-kernel "
                "clamp %s\n",
                InstallJobSlotFix() ? "given back" : "not installed",
                InstallResourceCacheSize() ? "in the port's memory" : "not installed",
                InstallHotEffects() ? "switched live" : "at start (patches)",
                InstallKernelSampleClamp() ? "on" : "not installed");
    const int camera = InstallCameraAspectHooks(), markers = InstallMarkerHooks();
    InstallMouseHooks();
    BbMenuPointer::Install();
    const bool ui = InstallUiHooks();
    std::printf("Game hooks: the output's aspect ratio: the camera at %d of %zu places, the HUD's "
                "markers at %d of %zu, the UI %s\n", camera, CameraParamStores.size(), markers,
                MarkerSites.size(), ui ? "fills the output (HUD at its edges)" : "in the middle");
}
} // namespace BbGameHooks
