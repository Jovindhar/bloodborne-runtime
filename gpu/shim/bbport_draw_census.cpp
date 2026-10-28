// SPDX-License-Identifier: GPL-2.0-or-later
// bloodborne-runtime: the draw census (bbport_draw_census.h). Entry hooks count calls; each
// boundary method makes one libGnm draw, so a libGnm draw whose return address is outside the
// boundary's functions reached libGnm another way, and its return address is tallied.
#include "bbport_draw_census.h"
#include "bbport_guest_code_hooks.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace BbDrawCensus {
namespace {
using u64 = std::uint64_t;
using u8 = std::uint8_t;

enum Kind : u64 { Boundary, Pass, Gnm, MeshDraw, Parameters };
struct Function {
    u64 offset, size;
    Kind kind;
    const char* name;
};
// Names and offsets: bloodborne-recomp's annotations/functions.tsv (1.09).
constexpr Function functions[] = {
    {0x21695c0, 113, Boundary, "CG::GraphicsContext::dispatch"},
    {0x21696c0, 180, Boundary, "CG::GraphicsContext::draw"},
    {0x2169780, 189, Boundary, "CG::GraphicsContext::drawIndexed"},
    {0x2169840, 181, Boundary, "CG::GraphicsContext::drawInstanced"},
    {0x2169900, 194, Boundary, "CG::GraphicsContext::drawIndexedInstanced"},
    {0x21699d0, 223, Boundary, "CG::GraphicsContext::drawIndirect"},
    {0x2169ab0, 226, Boundary, "CG::GraphicsContext::drawIndexedIndirect"},
    {0x26b55a0, 1228, Pass, "CG::SurfaceOps::resolve"},
    {0x26b5a70, 1236, Pass, "CG::SurfaceOps::eliminateFastClear"},
    {0x26b5f50, 2230, Pass, "CG::SurfaceOps::clearDepthStencil?"},
    {0x26b6810, 1174, Pass, "CG::SurfaceOps::decompressDepth"},
    {0x21d0640, 2621, MeshDraw, "CG::MeshRenderer::drawMesh"},
    {0x21c3f60, 566, Parameters, "FD4::MaterialParameters::begin"},
    {0x10740a0, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndex#inline?"},
    {0x1074360, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndex"},
    {0x10741b0, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndexAuto"},
    {0x1074420, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndexOffset"},
    {0x10744d0, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndirect"},
    {0x10746f0, 0, Gnm, "Gnm::DrawCommandBuffer::drawIndexIndirect"},
    {0x1074c50, 0, Gnm, "Gnm::DrawCommandBuffer::dispatch"},
    {0x1074de0, 0, Gnm, "Gnm::DrawCommandBuffer::dispatchIndirect"},
    {0x108a350, 0, Gnm, "Gnm::DispatchCommandBuffer::dispatch"},
};
constexpr std::size_t Count = sizeof(functions) / sizeof(functions[0]);

u64 image_base = 0;
std::atomic<u64> calls[Count];
std::atomic<u64> outside{0};
std::atomic<u64> last_report{0};

// Return addresses (open addressing): of the draws that reach libGnm outside the boundary, and of
// the calls of the boundary's methods (who draws through it).
constexpr std::size_t Slots = 512;
struct Callers {
    std::atomic<u64> address[Slots];
    std::atomic<u64> calls[Slots];
};
Callers outside_callers, boundary_callers, material_classes, parameter_classes;

u64 Now() {
    return u64(std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

void Tally(Callers& callers, u64 offset) {
    for (std::size_t i = 0, h = std::size_t((offset * 0x9e3779b97f4a7c15ull) >> 55); i < Slots; ++i) {
        const std::size_t slot = (h + i) % Slots;
        u64 expected = 0;
        if (callers.address[slot].load(std::memory_order_relaxed) == offset ||
            callers.address[slot].compare_exchange_strong(expected, offset, std::memory_order_relaxed)) {
            callers.calls[slot].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

// The busiest return addresses of a table, since the start.
void PrintTop(const Callers& callers, const char* what, int count) {
    u64 top[16][2] = {};
    for (std::size_t s = 0; s < Slots; ++s) {
        const u64 n = callers.calls[s].load(std::memory_order_relaxed);
        for (int k = 0; k < count; ++k) {
            if (n > top[k][1]) {
                for (int j = count - 1; j > k; --j) {
                    top[j][0] = top[j - 1][0];
                    top[j][1] = top[j - 1][1];
                }
                top[k][0] = callers.address[s].load(std::memory_order_relaxed);
                top[k][1] = n;
                break;
            }
        }
    }
    for (int k = 0; k < count; ++k) {
        if (top[k][1]) {
            std::printf("Draw census:   %s %#llx: %llu\n", what,
                        static_cast<unsigned long long>(top[k][0]), static_cast<unsigned long long>(top[k][1]));
        }
    }
}

void Report(u64 now) {
    static u64 previous[Count], previous_outside, previous_time;
    const double seconds = previous_time ? double(now - previous_time) / 1000.0 : 5.0;
    u64 sums[5] = {}, total[Count];
    for (std::size_t i = 0; i < Count; ++i) {
        total[i] = calls[i].load(std::memory_order_relaxed);
        sums[functions[i].kind] += total[i] - previous[i];
    }
    const u64 out = outside.load(std::memory_order_relaxed);
    std::printf("Draw census: %.0f libGnm draws/s; through the boundary %.0f/s, render-target passes %.0f/s, "
                "elsewhere %.0f/s (%.1f%%)\n",
                double(sums[Gnm]) / seconds, double(sums[Boundary]) / seconds, double(sums[Pass]) / seconds,
                double(out - previous_outside) / seconds,
                sums[Gnm] ? 100.0 * double(out - previous_outside) / double(sums[Gnm]) : 0.0);
    for (std::size_t i = 0; i < Count; ++i) {
        if (total[i] != previous[i]) {
            std::printf("Draw census:   %-45s %.0f/s\n", functions[i].name, double(total[i] - previous[i]) / seconds);
        }
        previous[i] = total[i];
    }
    PrintTop(outside_callers, "elsewhere, returning to", 8);
    PrintTop(boundary_callers, "through the boundary, returning to", 16);
    PrintTop(material_classes, "mesh draws with the material vtable at", 16);
    PrintTop(parameter_classes, "material parameters with the vtable at", 16);
    std::fflush(stdout);
    previous_outside = out;
    previous_time = now;
}

void __attribute__((sysv_abi)) Count_(u64 tag, const Core::GuestRegisterSnapshot* r) {
    calls[tag].fetch_add(1, std::memory_order_relaxed);
    // At the entry the return address is at rsp.
    const u64 back = *reinterpret_cast<const u64*>(r->rsp) - image_base;
    if (functions[tag].kind == Boundary) {
        Tally(boundary_callers, back);
    }
    if (functions[tag].kind == MeshDraw) {
        // drawMesh(model, pass, mesh, material, ...): the material's vtable, by image offset.
        const u64 material = r->rcx;
        if (material) {
            Tally(material_classes, *reinterpret_cast<const u64*>(material) - image_base);
        }
        return;
    }
    if (functions[tag].kind == Parameters) {
        // begin(parameters, ...): the vtables of the parameter objects in its list (+0xb8 to +0xc0).
        const u64 block = r->rdi;
        const u64* first = *reinterpret_cast<const u64* const*>(block + 0xb8);
        const u64* last = *reinterpret_cast<const u64* const*>(block + 0xc0);
        for (const u64* it = first; it && it < last && it - first < 64; ++it) {
            if (*it) {
                Tally(parameter_classes, *reinterpret_cast<const u64*>(*it) - image_base);
            }
        }
        return;
    }
    if (functions[tag].kind != Gnm) {
        return;
    }
    bool inside = false;
    for (const Function& f : functions) {
        if (f.kind != Gnm && back - f.offset < f.size) {
            inside = true;
            break;
        }
    }
    if (!inside) {
        outside.fetch_add(1, std::memory_order_relaxed);
        Tally(outside_callers, back);
    }
    const u64 now = Now();
    u64 last = last_report.load(std::memory_order_relaxed);
    if (now - last >= 5000 && last_report.compare_exchange_strong(last, now)) {
        Report(now);
    }
}
} // namespace

void PatchImage(unsigned char* image, std::uint64_t) {
    const char* env = std::getenv("BB_DRAW_CENSUS");
    if (!env || env[0] != '1') {
        return;
    }
    image_base = reinterpret_cast<u64>(image);
    last_report = Now();
    // push rbp; mov rbp, rsp; push r15 (or push r14): every one of them begins so.
    static constexpr u8 r15[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    static constexpr u8 r14[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56};
    int installed = 0;
    for (std::size_t i = 0; i < Count; ++i) {
        void* at = image + functions[i].offset;
        if (Core::InstallGuestCodeHook(at, r15, i, Count_) || Core::InstallGuestCodeHook(at, r14, i, Count_)) {
            ++installed;
        } else {
            std::printf("Draw census: no hook at %#llx (%s)\n", static_cast<unsigned long long>(functions[i].offset),
                        functions[i].name);
        }
    }
    std::printf("Draw census: %d of %zu functions counted\n", installed, Count);
}
} // namespace BbDrawCensus
