#include "nms_waypoints.h"

#include <algorithm>
#include <array>

namespace nms {

namespace {

struct WaypointRow {
    std::string_view long_name, zone;
    std::int32_t category, expansion;
};

constexpr WaypointRow kWaypoints[] = {
#define HAILSTONE_WAYPOINT(long_name, zone, category, expansion) {long_name, zone, category, expansion},
#include "nms_waypoints.inc"
#undef HAILSTONE_WAYPOINT
};

std::string_view base_name(std::string_view name) {
    if (is_current_zone(name)) name.remove_suffix(kCurrentZoneSuffix.size());
    return name;
}

const WaypointRow* find_row(const WaypointEntry& e) {
    std::string_view name = base_name(e.name);
    for (const auto& row : kWaypoints)
        if (row.long_name == name) return &row;
    return nullptr;
}

} // namespace

const char* continent_name(std::int32_t category) {
    static constexpr std::array<const char*, 9> kNames = {"Antonica", "Faydwer", "Odus",      "Kunark", "Velious",
                                                          "Luclin",   "The Planes", "Taelosia", "Discord"};
    return category >= 0 && category < static_cast<std::int32_t>(kNames.size()) ? kNames[category] : "Unknown";
}

const char* expansion_name(std::int32_t expansion) {
    static constexpr std::array<const char*, 22> kNames = {
        "Classic",           "Kunark",           "Velious",            "Luclin",
        "Planes of Power",   "Legacy of Ykesha", "Lost Dungeons of Norrath", "Gates of Discord",
        "Omens of War",      "Dragons of Norrath", "Depths of Darkhollow", "Prophecy of Ro",
        "The Serpent's Spine", "The Buried Sea", "Secrets of Faydwer", "Seeds of Destruction",
        "Underfoot",         "House of Thule",   "Veil of Alaris",     "Rain of Fear",
        "Call of the Forsaken", "The Darkened Sea"};
    return expansion >= 0 && expansion < static_cast<std::int32_t>(kNames.size()) ? kNames[expansion] : "Unknown";
}

bool is_current_zone(std::string_view name) {
    return name.size() >= kCurrentZoneSuffix.size() &&
           name.substr(name.size() - kCurrentZoneSuffix.size()) == kCurrentZoneSuffix;
}

std::int32_t waypoint_expansion(const WaypointEntry& e) {
    if (const WaypointRow* row = find_row(e); row && row->expansion >= 0) return row->expansion;
    switch (e.category) {
    case 0: case 1: case 2: return 0; // Antonica, Faydwer, Odus
    case 3: return 1;                 // Kunark
    case 4: return 2;                 // Velious
    case 5: return 3;                 // Luclin
    case 7: return 7;                 // Taelosia: Gates of Discord
    case 8: return 8;                 // Discord: Omens of War
    default: return -1;
    }
}

std::string_view waypoint_zone(const WaypointEntry& e) {
    const WaypointRow* row = find_row(e);
    return row ? row->zone : std::string_view{};
}

} // namespace nms
