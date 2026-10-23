// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: in-game settings menu (Dear ImGui), drawn by the presenter into the swapchain image
// after the game frame, at display resolution. Insert (keyboard) or L3+R3 (gamepad) opens it;
// while it is open the game gets no pad/keyboard input. Settings live in bbport_settings.h.

#pragma once

#include <string>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

union SDL_Event;
struct SDL_Window;

namespace Vulkan {
class Instance;
}

namespace BbOverlay {

/// Present thread, once: the ImGui context and its Vulkan backend.
void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count);

/// Window thread, for every SDL event: true when the menu consumed it.
bool HandleEvent(const SDL_Event& event);
/// Turns SDL text input on while the menu edits a value (window thread, once per poll).
void UpdateTextInput(SDL_Window* window);

/// Window thread, once per poll: fullscreen switched from the menu or the launcher, and the
/// monitor names the menu offers.
void ApplyWindowSettings(SDL_Window* window);

/// Guest thread: a newly persisted unlock, after the edited-save and duplicate checks.
void TrophyUnlocked(const char* title, int id);

/// Whether anything is drawn this frame (menu, FPS counter or trophy notice).
bool Visible();
/// A line of text over the game until called with null (bbgpu_overlay_banner).
void SetBanner(const char* text);

/// Present thread: draws into `view` (layout ColorAttachmentOptimal).
void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent);

/// The menu or the text dialog is open: the game's input is held neutral.
bool CapturesInput();

/// The settings menu is open: the window shows the system cursor over it.
bool MenuOpen();

/// Window thread: the game's text dialog (ImeDialog) state, drawn as a box over the frame.
void SetTextPrompt(bool active, const std::string& prompt, const std::string& text);

/// What the on-screen keyboard does to the text dialog's text (window thread, from HandleEvent).
enum class TextEdit { Append, Backspace, Done, Cancel };
/// The window's handler for those edits: the same text a physical keyboard types into.
void SetTextEditor(void (*edit)(TextEdit edit, const char* text, void* context), void* context);

} // namespace BbOverlay
