// The NMS module's commands and ImGui windows: the patch's /fov, the Personal Loot window's Loot tab and the waypoint
// window, each when no Lua window stands in for it.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

#include <imgui.h>

#include "net/game_connection.h"
#include "plugins/nms/nms_extension.h"
#include "ui/item_windows.h"
#include "ui/lua/lua_host.h"

namespace nmsc::nms {

std::optional<LootSend> loot_window(PersonalLoot& loot, bool reset, ui::ItemIconCache* icons) {
    // The Personal Loot window's Loot tab (plugins/nms/nms_loot.h), at the patch's default place and size, for a
    // UI without nmsloot.lua.
    std::optional<LootSend> act;
    if (!loot.visible()) return act;
    const std::int64_t now = net::GameConnection::now_ms();
    static const ImVec4 kDim{0.62f, 0.64f, 0.68f, 1.0f}; // the item windows' dim text
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(80, 80), reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 380), reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    const auto rows = loot.visible_rows(now);
    const std::string title = rows.empty() ? "Personal Loot###nmsc_ploot"
                                           : std::format("Personal Loot ({})###nmsc_ploot", rows.size());
    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (loot.prefs.locked) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (ImGui::Begin(title.c_str(), &open, flags)) {
        if (rows.empty()) ImGui::TextDisabled("No loot waiting. Kill something.");
        for (const PersonalLoot::Row* r : rows) {
            ImGui::PushID(static_cast<int>(r->d.pending_id));
            if (icons) icons->draw(r->d.icon, em * 1.6f, r->d.item_name.c_str());
            ImGui::SameLine();
            ImGui::Text("%s%s", r->d.item_name.c_str(),
                        r->d.quantity > 1 ? std::format(" x{}", r->d.quantity).c_str() : "");
            ImGui::SameLine();
            ImGui::TextColored(kDim, "(%s, %s)", r->d.npc_name.c_str(), loot_money(r->d.sell_value).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep")) act = LootSend{net::kLootActionKeep, r->d.pending_id, 0, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton(r->d.sell_value ? "Sell" : "Destroy"))
                act = LootSend{net::kLootActionSell, r->d.pending_id, 0, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton("Always Keep")) {
                loot.set_rule(r->d.item_id, r->d.item_name, LootRule::AlwaysKeep);
                act = LootSend{net::kLootActionKeep, r->d.pending_id, 0, 0};
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Always Sell")) {
                loot.set_rule(r->d.item_id, r->d.item_name, LootRule::AlwaysSell);
                act = LootSend{net::kLootActionSell, r->d.pending_id, 0, 0};
            }
            ImGui::PopID();
        }
        ImGui::BeginDisabled(rows.empty());
        if (ImGui::Button("Loot All")) act = LootSend{net::kLootActionLootAll, 0, 0, 0};
        ImGui::SameLine();
        if (ImGui::Button("Sell All")) act = LootSend{kLootActionSellAll, 0, 0, 0};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) act = LootSend{net::kLootActionRefresh, 0, 0, 0};
    }
    ImGui::End();
    if (!open) loot.show(false, now);
    return act;
}

// The NMS patch's /fov (and /camera) <10..150>: despite the name it sets the maximum camera zoom distance
// (__gfMaxZoomCameraDistance and __gfMaxCameraDistance, MQ2Commands.cpp CmdFov), not the field of view.
std::string NmsExtension::fov_command(const std::string& args, float& max_camera_distance, float& camera_distance) {
    std::string a = args;
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    if (a.empty()) return std::format("Current Max Zoom Distance is: {:f}", max_camera_distance);
    float v = std::strtof(a.c_str(), nullptr);
    if (v < 10.0f || v > 150.0f)
        return "Usage: /fov <zoom_distance> (min 10, max 150. Default is usually around 53. Try /fov 90)";
    max_camera_distance = v;
    camera_distance = std::min(camera_distance, v);
    return std::format("Max zoom distance set to {:f}", v);
}

bool NmsExtension::window_toggle(net::GameConnection&, const std::string& toggle, game::ExtensionAppContext& app) {
    auto print = [&app](const std::string& line) {
        if (app.print) app.print(line);
    };
    if (toggle.rfind("fov ", 0) == 0) {
        if (app.max_camera_distance && app.camera_distance)
            print(fov_command(toggle.substr(4), *app.max_camera_distance, *app.camera_distance));
        return true;
    }
    return false;
}

void NmsExtension::draw(net::GameConnection& c, game::ExtensionAppContext& app) {
    // The Lua Personal Loot window (nmsloot.lua) stands in for this one when the UI has it.
    if (c.state() == net::ConnectionState::InZone && !ui::lua::replaces("NmsLootWnd")) {
        const bool reset = loot_reset_seen_ != loot_reset_serial_;
        loot_reset_seen_ = loot_reset_serial_;
        if (auto act = loot_window(loot_, reset, app.item_icons)) send_loot(c, *act);
    }
    // The Lua WaypointsWnd (plugins/nms/nms_lua.cpp, the patch's SIDL window) stands in for this one when the UI
    // has it.
    if (waypoints_ && c.state() == net::ConnectionState::InZone && !ui::lua::replaces("WaypointsWnd")) {
        // WaypointsWnd::WndNotification: Travel sends the waypoint, My Expedition id 0 with the expedition flag,
        // and each check box id 0 with the boxes' state (the server stores them).
        WaypointAction act = waypoint_window(waypoint_window_, *waypoints_);
        switch (act.kind) {
        case WaypointAction::Kind::Travel: request_waypoint(c, act.id); break;
        case WaypointAction::Kind::Expedition: request_waypoint(c, 0, true); break;
        case WaypointAction::Kind::Settings: request_waypoint(c, 0); break;
        case WaypointAction::Kind::None: break;
        }
    }
}

} // namespace nmsc::nms
