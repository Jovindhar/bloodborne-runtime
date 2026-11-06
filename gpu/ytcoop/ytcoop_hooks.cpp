// SPDX-License-Identifier: GPL-2.0-or-later
// BloodOnline (Yharnam Together module): the guest code hooks of the co-op code (its
// core/cpu_patches.h), installed by this runtime's libbbgpu (gpu/shim/bbport_guest_code_hooks.cpp,
// through its C entry points). The Windows port writes x86 trampolines itself
// (bbcoop/guest_hooks.cpp); here the game's code runs through bbcpu on Apple Silicon, so the
// runtime's int3 stubs, which bbcpu knows, take their place.

#include <cstddef>
#include <cstdint>
#include <span>

#include "common/logging/log.h"
#include "core/cpu_patches.h"

extern "C" {
int bbgpu_guest_hook(int kind, void* address, const std::uint8_t* bytes, std::size_t size,
                     std::uint64_t tag, void* fn);
int bbgpu_guest_unhook(void* address);
}

namespace Core {
namespace {
enum HookKind { Observe = 0, RegisterEdit = 1, FunctionOverride = 2, FunctionResult = 3 };

bool Install(HookKind kind, void* address, std::span<const u8> expected, u64 tag, void* fn) {
    const bool ok =
        bbgpu_guest_hook(kind, address, expected.data(), expected.size(), tag, fn) != 0;
    if (!ok) {
        LOG_ERROR(Core, "Guest code hook at {} ({} bytes) did not go in", address,
                  expected.size());
    }
    return ok;
}
} // namespace

namespace WindowsGuestRedZoneProtection {
void SetActiveMode(WindowsGuestRedZoneProtectionMode) noexcept {}
WindowsGuestRedZoneProtectionMode GetActiveMode() noexcept {
    return WindowsGuestRedZoneProtectionMode::Disabled;
}
bool IsStaticPatchingEnabled() noexcept {
    return false;
}
} // namespace WindowsGuestRedZoneProtection

// The runtime keeps its own trampoline area next to the game's code.
void RegisterPatchModule(void*, u64, void*, u64) {}

bool InstallGuestCodeHook(void* address, std::span<const u8> expected, u64 tag,
                          GuestCodeHook hook) {
    return Install(Observe, address, expected, tag, reinterpret_cast<void*>(hook));
}

bool InstallGuestFunctionOverride(void* address, std::span<const u8> expected, u64 tag,
                                  GuestFunctionOverride hook) {
    return Install(FunctionOverride, address, expected, tag, reinterpret_cast<void*>(hook));
}

bool InstallGuestFunctionResult(void* address, std::span<const u8> expected, u64 tag,
                                GuestFunctionResult hook) {
    return Install(FunctionResult, address, expected, tag, reinterpret_cast<void*>(hook));
}

bool InstallGuestRegisterEditHook(void* address, std::span<const u8> expected, u64 tag,
                                  GuestRegisterEditHook hook) {
    return Install(RegisterEdit, address, expected, tag, reinterpret_cast<void*>(hook));
}

bool RemoveGuestCodeHook(void* address) {
    return bbgpu_guest_unhook(address) != 0;
}

void PrePatchInstructions(u64, u64) {}

RedZonePatchResult PatchRedZoneMemoryInstructions(u64, u64, std::span<const uintptr_t>) {
    return {};
}

} // namespace Core
