#include "plugins/nms/nms_waypoints.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <vector>

#include <imgui.h>

namespace nmsc::nms {

namespace {

struct WaypointRow {
    std::string_view long_name, zone;
    std::int32_t category, expansion;
};

constexpr WaypointRow kWaypoints[] = {
#define NMSC_WAYPOINT(long_name, zone, category, expansion) {long_name, zone, category, expansion},
#include "plugins/nms/nms_waypoints.inc"
#undef NMSC_WAYPOINT
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

bool contains_nocase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}

const ImVec4 kLocked{0.63f, 0.63f, 0.63f, 1.0f}; // the patch's 0xFFA0A0A0 for a locked waypoint
const ImVec4 kHere{0.55f, 0.85f, 1.0f, 1.0f};

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

WaypointAction waypoint_window(WaypointWindowState& state, WaypointList& list) {
    WaypointAction act;
    if (!state.show) return act;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 34.0f, em * 32.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Waypoints##nmsc_waypoints", &state.show)) {
        ImGui::End();
        return act;
    }

    struct Row {
        const WaypointEntry* e;
        std::int32_t expansion;
        bool here;
    };
    std::vector<Row> rows;
    rows.reserve(list.entries.size());
    std::map<std::int32_t, int> expansions, continents; // what the list has, with counts
    for (const auto& e : list.entries) {
        Row r{&e, waypoint_expansion(e), is_current_zone(e.name)};
        ++expansions[r.expansion];
        ++continents[e.category];
        rows.push_back(r);
    }

    // ---- filters ----
    auto combo = [](const char* label, std::int32_t& value, std::int32_t all, const char* all_text,
                    const std::map<std::int32_t, int>& present, const char* (*name)(std::int32_t)) {
        std::string preview = value == all ? all_text : name(value);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (ImGui::Selectable(all_text, value == all)) value = all;
            for (const auto& [v, n] : present) {
                std::string text = std::string(name(v)) + " (" + std::to_string(n) + ")";
                if (ImGui::Selectable(text.c_str(), value == v)) value = v;
            }
            ImGui::EndCombo();
        }
    };
    combo("Expansion", state.expansion, -2, "All expansions", expansions, &expansion_name);
    ImGui::SameLine();
    combo("Continent", state.continent, -1, "All continents", continents, &continent_name);
    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::InputTextWithHint("##search", "Search zones", state.search, sizeof state.search);
    ImGui::SameLine();
    ImGui::Checkbox("Show locked", &state.show_locked);

    // ---- the list ----
    std::vector<Row> shown;
    for (const auto& r : rows) {
        if (state.expansion != -2 && r.expansion != state.expansion) continue;
        if (state.continent != -1 && r.e->category != state.continent) continue;
        if (!state.show_locked && !r.e->enabled) continue;
        if (!contains_nocase(r.e->name, state.search) && !contains_nocase(waypoint_zone(*r.e), state.search)) continue;
        shown.push_back(r);
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    bool travel = false;
    if (ImGui::BeginTable("waypoints", 3, kFlags, ImVec2(0.0f, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 3.0f);
        ImGui::TableSetupColumn("Continent", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Expansion", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
            const bool asc = s.SortDirection != ImGuiSortDirection_Descending;
            std::stable_sort(shown.begin(), shown.end(), [&](const Row& a, const Row& b) {
                int c = 0;
                if (s.ColumnIndex == 1) c = a.e->category - b.e->category;
                else if (s.ColumnIndex == 2) c = a.expansion - b.expansion;
                if (c == 0) c = a.e->name.compare(b.e->name);
                return asc ? c < 0 : c > 0;
            });
        }
        for (const auto& r : shown) {
            const WaypointEntry& e = *r.e;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(e.id);
            const bool usable = e.enabled && !r.here;
            if (!usable) ImGui::PushStyleColor(ImGuiCol_Text, r.here ? kHere : kLocked);
            if (ImGui::Selectable(e.name.c_str(), state.selected == e.id,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                state.selected = e.id;
                if (usable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) travel = true;
            }
            if (!usable && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", r.here ? "You are here." : "Locked: find this waypoint in the world first.");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(continent_name(e.category));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(expansion_name(r.expansion));
            if (!usable) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // ---- the patch's buttons and check boxes ----
    const WaypointEntry* sel = nullptr;
    for (const auto& e : list.entries)
        if (e.id == state.selected) sel = &e;
    const bool can_travel = sel && sel->enabled && !is_current_zone(sel->name);
    ImGui::TextDisabled("%zu of %zu waypoints", shown.size(), list.entries.size());
    ImGui::BeginDisabled(!can_travel);
    if (ImGui::Button("Travel to selected")) travel = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!list.expedition_enabled);
    if (ImGui::Button("My expedition")) act.kind = WaypointAction::Kind::Expedition;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!list.group_enabled);
    if (ImGui::Checkbox("Port group", &list.group_selected)) act.kind = WaypointAction::Kind::Settings;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto accept", &list.autoconfirm_selected)) act.kind = WaypointAction::Kind::Settings;
    ImGui::End();

    if (travel && can_travel) act = {WaypointAction::Kind::Travel, sel->id};
    // The patch hides the window after Travel and My Expedition.
    if (act.kind == WaypointAction::Kind::Travel || act.kind == WaypointAction::Kind::Expedition) state.show = false;
    return act;
}

} // namespace nmsc::nms
