// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bbsc_persistence.h"
#include "core/bbsc_patch_tables.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/singleton.h"
#include "common/types.h"
#include "core/file_format/psf.h"
// bbport (macOS): the translator and the recompiled library learn of the code written here.
#include "bbport_guest_code_hooks.h"

namespace Core::Bbsc {
namespace {

using namespace PatchTables;

// Set once every enabled site has been byte-verified, so the runtime toggle can never write into
// an image the patches were rejected for.
bool patches_verified = false;
bool session_patches_on = false;
bool role_patches_on = false;

// Writes every enabled site in `scope` in its patched or its original form. Returns how many were
// written.
int WritePatches(bool enable, Scope scope) {
    const u64 base = MemoryPatcher::g_eboot_address;
    if (!patches_verified || base == 0) {
        return 0;
    }
    if (scope == Scope::Session && session_patches_on == enable) {
        return 0;
    }
    if (scope == Scope::Role && role_patches_on == enable) {
        return 0;
    }
    int count = 0;
    for (const auto& entry : EntryJumps) {
        if (!GroupEnabled(entry.group) || !InScope(entry.group, scope)) {
            continue;
        }
        auto* site = reinterpret_cast<u8*>(base + entry.site);
        if (enable) {
            const auto patched = EncodeRel32(0xE9, entry.site, entry.target);
            std::memcpy(site, patched.data(), patched.size());
        } else {
            std::memcpy(site, entry.prologue.data(), entry.prologue.size());
        }
        Core::GuestCodeWritten(site, std::max<size_t>(entry.prologue.size(), 5)); // bbport
        ++count;
    }
    for (const auto& entry : Retargets) {
        if (!GroupEnabled(entry.group) || !InScope(entry.group, scope)) {
            continue;
        }
        const auto bytes = EncodeRel32(entry.opcode, entry.site,
                                       enable ? entry.target : entry.original);
        std::memcpy(reinterpret_cast<u8*>(base + entry.site), bytes.data(), bytes.size());
        Core::GuestCodeWritten(reinterpret_cast<u8*>(base + entry.site), bytes.size()); // bbport
        ++count;
    }
    for (const auto& entry : EntryReturns) {
        if (!GroupEnabled(entry.group) || !InScope(entry.group, scope)) {
            continue;
        }
        // Only the first byte changes: 0xC3 (ret) or the function's real first instruction byte.
        *reinterpret_cast<u8*>(base + entry.site) = enable ? 0xC3 : entry.prologue[0];
        Core::GuestCodeWritten(reinterpret_cast<u8*>(base + entry.site), 1); // bbport
        ++count;
    }
    for (const auto& entry : ByteEdits) {
        if (!GroupEnabled(entry.group) || !InScope(entry.group, scope)) {
            continue;
        }
        const auto& bytes = enable ? entry.replacement : entry.original;
        std::memcpy(reinterpret_cast<u8*>(base + entry.site), bytes.data(), bytes.size());
        Core::GuestCodeWritten(reinterpret_cast<u8*>(base + entry.site), bytes.size()); // bbport
        ++count;
    }
    for (const auto& entry : WideEdits) {
        if (!GroupEnabled(entry.group) || !InScope(entry.group, scope)) {
            continue;
        }
        const auto& bytes = enable ? entry.replacement : entry.original;
        std::memcpy(reinterpret_cast<u8*>(base + entry.site), bytes.data(), bytes.size());
        Core::GuestCodeWritten(reinterpret_cast<u8*>(base + entry.site), bytes.size()); // bbport
        ++count;
    }
    if (scope == Scope::Install) {
        session_patches_on = enable;
    } else if (scope == Scope::Session) {
        session_patches_on = enable;
    } else {
        role_patches_on = enable;
    }
    return count;
}

// bbport (macOS): the sites switched while the game runs (session and role groups) stay with the
// translator; the recompiled library would keep running their functions as they were when it
// loaded. Sites written once at install change the code before it loads, which it checks.
void ExcludeSwitchedSites(u64 base) {
    const auto exclude = [base](Group group, u64 site, size_t size) {
        if (GroupEnabled(group) && (IsSessionGroup(group) || IsRoleGroup(group))) {
            Core::ExcludeGuestCodeFromRecompiled(reinterpret_cast<const void*>(base + site), size);
        }
    };
    for (const auto& entry : EntryJumps) {
        exclude(entry.group, entry.site, std::max<size_t>(entry.prologue.size(), 5));
    }
    for (const auto& entry : Retargets) {
        exclude(entry.group, entry.site, 5);
    }
    for (const auto& entry : EntryReturns) {
        exclude(entry.group, entry.site, 1);
    }
    for (const auto& entry : ByteEdits) {
        exclude(entry.group, entry.site, entry.replacement.size());
    }
    for (const auto& entry : WideEdits) {
        exclude(entry.group, entry.site, entry.replacement.size());
    }
}

} // namespace

void SetPersistentSessionPatches(bool enabled) {
    const int count = WritePatches(enabled, Scope::Session);
    if (count != 0) {
        LOG_INFO(Debug, "BBSC persistence {} ({} session sites; echoes, lamp, fog walls and "
                        "deaths untouched)",
                 enabled ? "re-applied" : "suspended", count);
    }
}

void SetHostLampPatches(bool hosting) {
    const int count = WritePatches(hosting, Scope::Role);
    if (count != 0) {
        LOG_INFO(Debug, "BBSC lamp: {} ({} sites)",
                 hosting ? "prompt allowed (hosting a session)"
                         : "vanilla gates (solo or guest - a guest never gets the lamp)",
                 count);
    }
}

void InstallPersistentSessionPatches() {
    static bool attempted = false;
    if (attempted) {
        return;
    }
    if (!EnvFlag("SHADPS4_BBSC_PERSIST")) {
        LOG_INFO(Debug, "BBSC persistence declined: SHADPS4_BBSC_PERSIST is off");
        return;
    }
    attempted = true;

    if (MemoryPatcher::g_game_serial != "CUSA03173") {
        LOG_INFO(Debug, "BBSC persistence declined: this is {}, not CUSA03173",
                 MemoryPatcher::g_game_serial.empty() ? "an unknown title"
                                                      : MemoryPatcher::g_game_serial);
        return;
    }
    const auto* param_sfo = Common::Singleton<PSF>::Instance();
    const std::string_view app_version = param_sfo->GetString("APP_VER").value_or("Unknown");
    if (app_version != "01.09") {
        LOG_ERROR(Debug, "BBSC persistence requires CUSA03173 01.09; loaded version is {}",
                  app_version);
        return;
    }

    const u64 base = MemoryPatcher::g_eboot_address;
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (base == 0) {
        LOG_ERROR(Debug, "BBSC persistence declined: the eboot image base is not known yet");
        return;
    }

    // Verify every enabled site before writing any, so a mismatch leaves the game untouched.
    for (const auto& entry : Retargets) {
        if (!GroupEnabled(entry.group)) {
            continue;
        }
        if (entry.site + 5 > image_size) {
            LOG_ERROR(Debug, "BBSC persistence site {} is outside the image", entry.name);
            return;
        }
        const auto original = EncodeRel32(entry.opcode, entry.site, entry.original);
        const auto patched = EncodeRel32(entry.opcode, entry.site, entry.target);
        const auto* actual = reinterpret_cast<const u8*>(base + entry.site);
        if (std::memcmp(actual, original.data(), 5) != 0 &&
            std::memcmp(actual, patched.data(), 5) != 0) {
            LOG_ERROR(Debug,
                      "BBSC persistence signature mismatch at {} {:#x}; no persistence patches "
                      "applied",
                      entry.name, entry.site);
            return;
        }
    }
    for (const auto& entry : EntryReturns) {
        if (!GroupEnabled(entry.group)) {
            continue;
        }
        if (entry.site + entry.prologue.size() > image_size) {
            LOG_ERROR(Debug, "BBSC persistence site {} is outside the image", entry.name);
            return;
        }
        const auto* actual = reinterpret_cast<const u8*>(base + entry.site);
        if (actual[0] != 0xC3 &&
            std::memcmp(actual, entry.prologue.data(), entry.prologue.size()) != 0) {
            LOG_ERROR(Debug,
                      "BBSC persistence signature mismatch at {} {:#x}; no persistence patches "
                      "applied",
                      entry.name, entry.site);
            return;
        }
    }

    for (const auto& entry : EntryJumps) {
        if (!GroupEnabled(entry.group)) {
            continue;
        }
        if (entry.site + entry.prologue.size() > image_size || entry.target >= image_size) {
            LOG_ERROR(Debug, "BBSC persistence site {} is outside the image", entry.name);
            return;
        }
        const auto patched = EncodeRel32(0xE9, entry.site, entry.target);
        const auto* actual = reinterpret_cast<const u8*>(base + entry.site);
        if (std::memcmp(actual, patched.data(), patched.size()) != 0 &&
            std::memcmp(actual, entry.prologue.data(), entry.prologue.size()) != 0) {
            LOG_ERROR(Debug,
                      "BBSC persistence signature mismatch at {} {:#x}; no persistence patches "
                      "applied",
                      entry.name, entry.site);
            return;
        }
    }

    for (const auto& entry : ByteEdits) {
        if (!GroupEnabled(entry.group)) {
            continue;
        }
        if (entry.site + entry.original.size() > image_size) {
            LOG_ERROR(Debug, "BBSC persistence site {} is outside the image", entry.name);
            return;
        }
        const auto* actual = reinterpret_cast<const u8*>(base + entry.site);
        if (std::memcmp(actual, entry.original.data(), entry.original.size()) != 0 &&
            std::memcmp(actual, entry.replacement.data(), entry.replacement.size()) != 0) {
            LOG_ERROR(Debug,
                      "BBSC persistence signature mismatch at {} {:#x}; no persistence patches "
                      "applied",
                      entry.name, entry.site);
            return;
        }
    }

    for (const auto& entry : WideEdits) {
        if (!GroupEnabled(entry.group)) {
            continue;
        }
        if (entry.site + entry.original.size() > image_size) {
            LOG_ERROR(Debug, "BBSC persistence site {} is outside the image", entry.name);
            return;
        }
        const auto* actual = reinterpret_cast<const u8*>(base + entry.site);
        if (std::memcmp(actual, entry.original.data(), entry.original.size()) != 0 &&
            std::memcmp(actual, entry.replacement.data(), entry.replacement.size()) != 0) {
            LOG_ERROR(Debug,
                      "BBSC persistence signature mismatch at {} {:#x}; no persistence patches "
                      "applied",
                      entry.name, entry.site);
            return;
        }
    }

    patches_verified = true;
    ExcludeSwitchedSites(base); // bbport
    const int applied = WritePatches(true, Scope::Install);
    LOG_INFO(Debug,
             "BBSC persistence enabled ({} sites; boss={}, matching-check={}, lamp={}, "
             "keep-tasks={}, ignore-host-death={}, no-fog-walls={}, skip-leave-limit={}, "
             "death-without-reload={}, guest-items={}, solid-npcs={})",
             applied, GroupEnabled(Group::Boss), GroupEnabled(Group::MatchCheck),
             GroupEnabled(Group::Lamp), GroupEnabled(Group::Tasks),
             GroupEnabled(Group::HostDead), GroupEnabled(Group::NoFogWalls),
             GroupEnabled(Group::LeaveLimit), GroupEnabled(Group::Spectate),
             GroupEnabled(Group::GuestItems), GroupEnabled(Group::SolidNpcs));
}

} // namespace Core::Bbsc
