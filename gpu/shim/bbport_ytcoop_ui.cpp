// SPDX-License-Identifier: GPL-2.0-or-later
// BloodOnline: the Yharnam Together screens over the game (the online module gpu/ytcoop, its
// bbcoop.h C entry points). A plain version of Yharnam Together's own team screen
// (its bbport_overlay.cpp Team, Joining and TeamTravel): the team lobby (create, join by code,
// invites, the world, Start or play alone), the team's loading rows and the team's travel
// question. Nothing is shown unless the loaded online module is Yharnam Together's.

#include "bbport_ytcoop_ui.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>

#include "imgui.h"

// The module's types (gpu/ytcoop/vendor/bbport/gpu/bbcoop.h), declared by its own header.
#include "../ytcoop/vendor/bbport/gpu/bbcoop.h"

namespace BbYtCoop {
namespace {

struct Api {
    bool loaded = false;
    int (*team_screen)() = nullptr;
    int (*lobby_view)(bbcoop_lobby*) = nullptr;
    void (*lobby_request)(int, const char*) = nullptr;
    void (*team_choose)(int) = nullptr;
    void (*team_menu)(int) = nullptr;
    void (*team_join_hunt)(const char*) = nullptr;
    int (*team_load_view)(bbcoop_team_load*) = nullptr;
    int (*team_travel_view)(bbcoop_team_travel*) = nullptr;
    void (*team_travel_answer)(int) = nullptr;
    int (*party_panel)(bbcoop_panel*) = nullptr;
};

// The runtime keeps the online module's handle (src/runtime_net.c); null before it is loaded or
// when it is libbbnet20.
void* ModuleSymbol(const char* name) {
    using Lookup = void* (*)(const char*);
    static const Lookup lookup =
        reinterpret_cast<Lookup>(dlsym(RTLD_DEFAULT, "runtime_net_module_symbol"));
    return lookup ? lookup(name) : nullptr;
}

const Api& Get() {
    static Api api;
    if (api.loaded) {
        return api;
    }
    // Only once the module answers: it is loaded at the game's start, after the overlay.
    void* view = ModuleSymbol("bbcoop_lobby_view");
    if (!view) {
        return api;
    }
    auto sym = [](auto& out, const char* name) {
        out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(ModuleSymbol(name));
    };
    sym(api.team_screen, "bbcoop_team_screen");
    sym(api.lobby_view, "bbcoop_lobby_view");
    sym(api.lobby_request, "bbcoop_lobby_request");
    sym(api.team_choose, "bbcoop_team_choose");
    sym(api.team_menu, "bbcoop_team_menu");
    sym(api.team_join_hunt, "bbcoop_team_join_hunt");
    sym(api.team_load_view, "bbcoop_team_load_view");
    sym(api.team_travel_view, "bbcoop_team_travel_view");
    sym(api.team_travel_answer, "bbcoop_team_travel_answer");
    sym(api.party_panel, "bbcoop_party_panel");
    api.loaded = api.team_screen && api.lobby_view && api.lobby_request && api.team_choose &&
                 api.team_menu && api.team_join_hunt && api.team_load_view &&
                 api.team_travel_view && api.team_travel_answer;
    return api;
}

// Read once per frame by Draw; the event thread only reads the flags.
bbcoop_lobby lobby{};
bbcoop_team_load load{};
bbcoop_team_travel travel{};
bool team_open = false, load_open = false, travel_open = false;
std::atomic<bool> captures{false};    // read by the window thread
std::atomic<bool> menu_screen{false}; // the team screen opened in game (closes on Escape)
char code_input[16] = "";
char invite_input[40] = "";

const char* StateName(int state) {
    switch (state) {
    case BBCOOP_LOBBY_TITLE:
        return "at the title";
    case BBCOOP_LOBBY_PLAYING:
        return "playing";
    default:
        return "offline";
    }
}

/// The team screens are drawn larger than the in-game menu (players asked for it): text at
/// kTextScale of the overlay's size, and widths in 1080p pixels scaled with the display.
constexpr float kTextScale = 1.4f;

float Px(float px) {
    const float display = std::max(ImGui::GetIO().DisplaySize.y / 1080.0f, 0.75f);
    return px * display * kTextScale;
}

void BeginCentered(const char* title, float base_width) {
    const ImGuiIO& io = ImGui::GetIO();
    // At least 45% of the display's width, never more than 90%.
    const float width =
        std::min(std::max(Px(base_width), io.DisplaySize.x * 0.45f), io.DisplaySize.x * 0.9f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, io.DisplaySize.y * 0.9f));
    ImGui::Begin(title, nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
}

void TeamScreen(const Api& api) {
    BeginCentered("Yharnam Together: team", 560.0f);
    if (!lobby.signed_in) {
        ImGui::TextWrapped("Connecting to the host's Yharnam Together server...");
    }
    if (lobby.notice[0]) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", lobby.notice);
    }

    if (lobby.in_team) {
        ImGui::SeparatorText("Your team");
        for (int i = 0; i < lobby.member_count; ++i) {
            const bbcoop_lobby_entry& m = lobby.members[i];
            ImGui::PushID(i);
            ImGui::BulletText("%s%s  (%s%s)", m.name, i == 0 ? "  [leader]" : "",
                              StateName(m.state), m.ready ? ", ready" : "");
            if (lobby.leader && i > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    api.lobby_request(BBCOOP_TEAM_REMOVE, m.id);
                }
            }
            ImGui::PopID();
        }
        if (lobby.leader && lobby.code[0]) {
            ImGui::Text("Team code: %s", lobby.code);
            ImGui::SameLine();
            if (ImGui::SmallButton("New code")) {
                api.lobby_request(BBCOOP_TEAM_NEW_CODE, "");
            }
        }
        if (lobby.leader) {
            ImGui::SeparatorText("World");
            const char* current = "A new world";
            if (lobby.world >= 0 && lobby.world < lobby.world_count) {
                current = lobby.worlds[lobby.world].name;
            }
            if (ImGui::BeginCombo("##world", current)) {
                if (ImGui::Selectable("A new world", lobby.world < 0)) {
                    api.lobby_request(BBCOOP_TEAM_WORLD, "");
                }
                for (int i = 0; i < lobby.world_count && i < BBCOOP_LOBBY_WORLDS; ++i) {
                    const bbcoop_lobby_world& w = lobby.worlds[i];
                    char label[128];
                    std::snprintf(label, sizeof(label), "%s  (%d lamps%s%s)##%d", w.name, w.lamps,
                                  w.when[0] ? ", " : "", w.when, i);
                    if (ImGui::Selectable(label, lobby.world == i)) {
                        api.lobby_request(BBCOOP_TEAM_WORLD, w.id);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SeparatorText("Invite");
            for (int i = 0; i < lobby.player_count && i < BBCOOP_LOBBY_PLAYERS; ++i) {
                const bbcoop_lobby_entry& p = lobby.players[i];
                if (p.in_my_team) {
                    continue;
                }
                ImGui::PushID(100 + i);
                ImGui::BulletText("%s  (%s)", p.name, StateName(p.state));
                ImGui::SameLine();
                if (ImGui::SmallButton("Invite")) {
                    api.lobby_request(BBCOOP_TEAM_INVITE, p.id);
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(Px(260.0f));
            ImGui::InputTextWithHint("##invite", "Player name", invite_input,
                                     sizeof(invite_input));
            ImGui::SameLine();
            if (ImGui::Button("Invite by name") && invite_input[0]) {
                api.lobby_request(BBCOOP_TEAM_INVITE_NAME, invite_input);
                invite_input[0] = '\0';
            }
        }
    } else {
        ImGui::SeparatorText("No team yet");
        if (ImGui::Button("Create a team")) {
            api.lobby_request(BBCOOP_TEAM_CREATE, "");
        }
        ImGui::SetNextItemWidth(Px(160.0f));
        ImGui::InputTextWithHint("##code", "Team code", code_input, sizeof(code_input));
        ImGui::SameLine();
        if (ImGui::Button("Join by code") && code_input[0]) {
            api.lobby_request(BBCOOP_TEAM_JOIN_CODE, code_input);
            code_input[0] = '\0';
        }
        for (int i = 0; i < lobby.open_count && i < BBCOOP_LOBBY_OPEN; ++i) {
            const bbcoop_lobby_open& t = lobby.open_teams[i];
            ImGui::PushID(200 + i);
            ImGui::BulletText("%s's team  (%d/%d%s)", t.name, t.members, lobby.team_size,
                              t.started ? ", hunting" : "");
            ImGui::SameLine();
            if (t.asked) {
                if (ImGui::SmallButton("Cancel")) {
                    api.lobby_request(BBCOOP_TEAM_ASK_CANCEL, t.id);
                }
            } else if (ImGui::SmallButton(t.open ? "Join" : "Ask to join")) {
                api.lobby_request(BBCOOP_TEAM_ASK_JOIN, t.id);
            }
            ImGui::PopID();
        }
    }

    if (lobby.invite_count > 0) {
        ImGui::SeparatorText("Invitations");
        for (int i = 0; i < lobby.invite_count && i < BBCOOP_LOBBY_INVITES; ++i) {
            const bbcoop_lobby_entry& inv = lobby.invites[i];
            ImGui::PushID(300 + i);
            ImGui::BulletText("From %s", inv.name);
            ImGui::SameLine();
            if (ImGui::SmallButton("Accept")) {
                if (lobby.menu && lobby.playing) {
                    api.team_join_hunt(inv.id); // in game: accept and join where this save stands
                } else {
                    api.lobby_request(BBCOOP_TEAM_ACCEPT, inv.id);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Decline")) {
                api.lobby_request(BBCOOP_TEAM_DECLINE, inv.id);
            }
            ImGui::PopID();
        }
    }
    if (lobby.leader && lobby.ask_count > 0) {
        ImGui::SeparatorText("Asking to join");
        for (int i = 0; i < lobby.ask_count && i < BBCOOP_LOBBY_ASKS; ++i) {
            const bbcoop_lobby_entry& a = lobby.asks[i];
            ImGui::PushID(400 + i);
            ImGui::BulletText("%s", a.name);
            ImGui::SameLine();
            if (ImGui::SmallButton("Accept")) {
                api.lobby_request(BBCOOP_TEAM_ASK_ACCEPT, a.id);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Decline")) {
                api.lobby_request(BBCOOP_TEAM_ASK_DECLINE, a.id);
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    if (lobby.menu) {
        // Opened in game (the menu): join the team's world where this save stands, or close.
        if (lobby.in_team && !lobby.in_hunt && ImGui::Button("Join the hunt")) {
            api.team_join_hunt("");
        }
        if (lobby.in_team) {
            ImGui::SameLine();
            if (ImGui::Button("Leave team")) {
                api.lobby_request(BBCOOP_TEAM_LEAVE, "");
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Close")) {
            api.team_menu(0);
        }
    } else {
        // The title holds the save's load for the team.
        if (lobby.in_team) {
            ImGui::BeginDisabled(!lobby.leader);
            if (ImGui::Button(lobby.leader ? "Start the hunt" : "Waiting for the leader")) {
                api.team_choose(BBCOOP_TEAM_GO_START);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Leave team")) {
                api.lobby_request(BBCOOP_TEAM_LEAVE, "");
            }
        } else if (ImGui::Button("Play alone")) {
            api.team_choose(BBCOOP_TEAM_GO_ALONE);
        }
    }
    ImGui::End();
}

void LoadScreen() {
    BeginCentered("Yharnam Together: loading", 420.0f);
    for (int i = 0; i < load.count && i < BBCOOP_LOBBY_TEAM; ++i) {
        const bbcoop_team_load_row& row = load.rows[i];
        static const char* const stages[] = {"Waiting", "Loading", "Joining", "Ready"};
        const char* stage = row.stage >= 0 && row.stage < 4 ? stages[row.stage] : "";
        ImGui::Text("%s%s", row.name, row.self ? " (you)" : "");
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%s %d%%", stage, row.percent);
        ImGui::ProgressBar(row.percent / 100.0f, ImVec2(-1, 0), overlay);
    }
    ImGui::End();
}

void TravelQuestion(const Api& api) {
    BeginCentered("Yharnam Together: travel", 420.0f);
    if (travel.question) {
        ImGui::TextWrapped("%s wants the team to travel to another lamp (%.0f s).", travel.from,
                           travel.seconds_left);
        if (ImGui::Button("Travel")) {
            api.team_travel_answer(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stay")) {
            api.team_travel_answer(0);
        }
    } else {
        ImGui::TextWrapped("Waiting for the team (%.0f s)...", travel.seconds_left);
    }
    ImGui::End();
}

} // namespace

bool Available() {
    return Get().loaded;
}

void Update() {
    const Api& api = Get();
    if (!api.loaded) {
        team_open = load_open = travel_open = false;
        captures.store(false, std::memory_order_relaxed);
        menu_screen.store(false, std::memory_order_relaxed);
        return;
    }
    team_open = api.team_screen() != 0 && api.lobby_view(&lobby) != 0;
    load_open = api.team_load_view(&load) != 0;
    travel_open = api.team_travel_view(&travel) != 0 && (travel.question || travel.waiting);
    captures.store(team_open || (travel_open && travel.question), std::memory_order_relaxed);
    menu_screen.store(team_open && lobby.menu, std::memory_order_relaxed);
}

bool WantsDraw() {
    return team_open || load_open || travel_open;
}

bool CapturesInput() {
    return captures.load(std::memory_order_relaxed);
}

void OpenTeam() {
    const Api& api = Get();
    if (api.loaded) {
        api.team_menu(2); // from the port's menu: no page of the game under it
    }
}

void Close() {
    const Api& api = Get();
    if (api.loaded && menu_screen.load(std::memory_order_relaxed)) {
        api.team_menu(0);
    }
}

void Draw() {
    const Api& api = Get();
    if (!api.loaded) {
        return;
    }
    ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * kTextScale);
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle saved = style;
    style.WindowPadding = ImVec2(style.WindowPadding.x * kTextScale, style.WindowPadding.y * kTextScale);
    style.FramePadding = ImVec2(style.FramePadding.x * kTextScale, style.FramePadding.y * kTextScale);
    style.ItemSpacing = ImVec2(style.ItemSpacing.x * kTextScale, style.ItemSpacing.y * kTextScale);
    if (load_open) {
        LoadScreen();
    } else if (team_open) {
        TeamScreen(api);
    }
    if (travel_open) {
        TravelQuestion(api);
    }
    style = saved;
    ImGui::PopFont();
}

} // namespace BbYtCoop
