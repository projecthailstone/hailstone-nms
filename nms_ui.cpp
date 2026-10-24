// The NMS module's commands and windows: the patch's /fov and /nmsloot, and its "NMS loot window" for the server's
// personal loot list (NMS/dinput8.dll strings; rof2decomp notes/nms-dinput8.md).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

#include <imgui.h>

#include "net/game_connection.h"
#include "plugins/nms/nms_extension.h"
#include "ui/item_windows.h"

namespace nmsc::nms {

std::optional<std::pair<std::uint32_t, std::uint32_t>> loot_window(LootWindowState& state,
                                                                   const net::PersonalLootList& list,
                                                                   ui::ItemIconCache* icons) {
    // ---- NMS personal loot (Custom:Loot_Enabled): the NMS client patch's "NMS loot window" (/nmsloot), at the
    // place its own ImGui settings give it (nms_imgui.ini: 6,513), kept on screen.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> act;
    if (list.rows.empty() || !state.show) return act;
    static const ImVec4 kDim{0.62f, 0.64f, 0.68f, 1.0f}; // the item windows' dim text
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 at(vp->WorkPos.x + 6.0f, std::min(vp->WorkPos.y + 513.0f, vp->WorkPos.y + vp->WorkSize.y * 0.6f));
    ImGui::SetNextWindowPos(at, state.reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    state.reset = false;
    if (ImGui::Begin("NMS loot window##nmsc_ploot", &state.show,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const auto& row : list.rows) {
            ImGui::PushID(static_cast<int>(row.pending_id));
            if (icons) icons->draw(row.icon, em * 1.6f, row.item_name.c_str());
            ImGui::SameLine();
            ImGui::Text("%s%s", row.item_name.c_str(), row.quantity > 1 ? std::format(" x{}", row.quantity).c_str() : "");
            ImGui::SameLine();
            ImGui::TextColored(kDim, "(%s, %us)", row.npc_name.c_str(), row.seconds_left);
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep")) act = {net::kLootActionKeep, row.pending_id};
            ImGui::SameLine();
            if (ImGui::SmallButton(row.sell_value ? std::format("Sell {}cp", row.sell_value).c_str() : "Destroy"))
                act = {net::kLootActionSell, row.pending_id};
            ImGui::SameLine();
            if (ImGui::SmallButton("Inspect")) act = {net::kLootActionInspect, row.pending_id};
            ImGui::PopID();
        }
        if (ImGui::Button("Keep all")) act = {net::kLootActionLootAll, 0};
    }
    ImGui::End();
    return act;
}

std::vector<std::string> NmsExtension::loot_command(const std::string& args) {
    // The NMS client patch's /nmsloot (dinput8.dll strings); popout / dock move the patch's window to its own OS
    // window, which this client does not do.
    std::string a = args;
    while (!a.empty() && a.back() == ' ') a.pop_back();
    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (a.empty()) loot_window_.show = !loot_window_.show;
    else if (a == "show" || a == "open") loot_window_.show = true;
    else if (a == "hide" || a == "close") loot_window_.show = false;
    else if (a == "reset") loot_window_.show = true, loot_window_.reset = true;
    else if (a == "help")
        return {"  /nmsloot        - Toggle window visibility", "  /nmsloot show   - Open/show the loot window",
                "  /nmsloot open   - Open/show the loot window", "  /nmsloot hide   - Close/hide the loot window",
                "  /nmsloot close  - Close/hide the loot window",
                "  /nmsloot reset  - Reset window position/size to default and show it",
                "  /nmsloot help   - Show this help message"};
    else if (a == "popout" || a == "dock") return {"[NMS] The loot window is not popped out."};
    else return {"Unknown argument: '" + args + "'. Type '/nmsloot help' for usage."};
    return {};
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
    if (toggle.rfind("nmsloot ", 0) == 0) {
        for (auto& line : loot_command(toggle.substr(8))) print(line);
        return true;
    }
    return false;
}

void NmsExtension::draw(net::GameConnection& c, game::ExtensionAppContext& app) {
    if (const auto& pl = c.personal_loot())
        if (auto act = loot_window(loot_window_, *pl, app.item_icons)) c.personal_loot_action(act->first, act->second);
    if (waypoints_ && c.state() == net::ConnectionState::InZone) {
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
