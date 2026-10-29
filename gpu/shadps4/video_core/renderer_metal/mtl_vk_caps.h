// SPDX-License-Identifier: GPL-2.0-or-later
// The Metal backend's answers to the Vulkan queries the renderer makes at start (features,
// properties and limits, formats, memory, queue families, extensions), from the MTLDevice
// (docs/RENDER_ADAPTER.md, M4 step 4, decision 6). BB_METAL_CAPS=compare computes them next to
// MoltenVK's and logs every difference; MoltenVK's stay in use.

#pragma once


namespace MetalCaps {

/// With BB_METAL_CAPS=compare: compares the answers for `physical_device` (a VkPhysicalDevice,
/// MoltenVK's) and logs the differences. Otherwise nothing.
void CompareIfAsked(void* physical_device);

/// Right after the dispatcher loads the instance's functions: with the Metal renderer
/// (BB_METAL_FRAME=1) the physical-device queries are answered by the Metal backend (M4 step 4.2;
/// BB_METAL_CAPS=moltenvk keeps MoltenVK's). The comparison still asks MoltenVK.
void UseMetalAnswers();

/// Physical-device entry points for the loader-free Metal instance.
void* GetProc(const char* name);

} // namespace MetalCaps
