#pragma once

// The NMS waypoint window (the patch's WaypointsWnd, NMS/dinput8.dll waypoint_window.cpp): the server's waypoints
// (OP_WaypointList 0x1402, sent when the player clicks the Magic Map or a waypoint) to travel to with
// OP_WaypointRequest 0x1403. The patch lists them by continent; this window also filters them by expansion and by
// name. OP_WaypointList carries only each waypoint's long name and continent, so the expansion comes from the
// server's tables (nms_waypoints.inc, tools/gen_nms_waypoints.py), and from the continent for a waypoint added since.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "plugins/nms/nms_packets.h"

namespace nmsc::nms {

// The server's nms_waypoints_categories names. The patch's own table has "Discord" at 7 and "Special" at 8; the
// server's waypoints are Taelosia (7) and Discord (8).
const char* continent_name(std::int32_t category);
// EQEmu's expansion numbers (zone.expansion): 0 "Classic", 1 "Kunark", ... 8 "Omens of War", ...; "Unknown" else.
const char* expansion_name(std::int32_t expansion);

// The server appends this to the player's own zone (Client::SendWaypointList), and sends it disabled.
inline constexpr std::string_view kCurrentZoneSuffix = " [Current Zone]";
bool is_current_zone(std::string_view name);
// The waypoint's expansion: its row in nms_waypoints.inc by long name (without kCurrentZoneSuffix), else the
// expansion of its continent (Antonica, Faydwer and Odus Classic; Kunark, Velious, Luclin, Taelosia GoD, Discord
// OoW), else -1 (The Planes span expansions).
std::int32_t waypoint_expansion(const WaypointEntry& e);
// The zone short name from nms_waypoints.inc; empty when the waypoint is not in it.
std::string_view waypoint_zone(const WaypointEntry& e);

struct WaypointWindowState {
    bool show = false;
    std::int32_t expansion = -2; // the filter: -2 all
    std::int32_t continent = -1; // the filter: -1 all
    char search[64] = {};
    bool show_locked = true;
    std::int32_t selected = 0;   // the waypoint id; 0 none
};

struct WaypointAction {
    enum class Kind { None, Travel, Expedition, Settings } kind = Kind::None;
    std::int32_t id = 0; // Travel: the waypoint
};

// Draws the window while state.show; may change list.group_selected / autoconfirm_selected (Settings).
WaypointAction waypoint_window(WaypointWindowState& state, WaypointList& list);

} // namespace nmsc::nms
