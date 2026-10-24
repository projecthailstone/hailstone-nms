// The NMS client patch's own windows (NMS/dinput8.dll, rof2decomp notes/nms-dinput8.md §4.3) over the NMS
// extension's state (plugins/nms/nms_extension.h):
//   - WaypointsWnd: a SIDL window of the patch's own XML (uifiles/default/NMS_WaypointsWnd.xml, AddXMLFile), drawn by
//     nmswaypoints.lua from its generated layout; its code is the patch's WaypointsWnd (NMS-Release
//     Release-NMS-Client/eqgame_dll/waypoint_window.cpp, checked against the shipped DLL: FUN_101954a0 shows the
//     window on every OP_WaypointList, FUN_101955b0 fills it as the source does). While it runs, the module's ImGui
//     waypoint window (plugins/nms/nms_waypoints.h) stays off.
//   - eq.NmsCooldowns: the rows of the patch's ImGui "Autoplay Cooldowns" window (FUN_1000a960), drawn by
//     nmscooldowns.lua.

#include "plugins/nms/nms_lua.h"

#include <set>
#include <string>
#include <vector>

#include <lua.hpp>

#include "app/lua_eq.h"
#include "app/lua_eq_windows.h"
#include "net/game_connection.h"
#include "plugins/nms/nms_extension.h"

namespace nmsc::nms {

namespace {

// The connection's NMS extension when it runs (an NMS server), else null.
NmsExtension* ext(app::EqHost& host) {
    if (!host.conn || !host.conn->nms_mode()) return nullptr;
    return dynamic_cast<NmsExtension*>(&host.conn->server_ext());
}

// WaypointsWnd::WaypointsWnd: the continent names by category id (0-based); others are "Unknown". (The patch's
// own table, not the server's categories, plugins/nms/nms_waypoints.h continent_name.)
std::string patch_continent_name(std::int32_t id) {
    static const char* const kNames[] = {"Antonica", "Faydwer", "Odus",      "Kunark", "Velious",
                                         "Luclin",   "The Planes", "Discord", "Special"};
    return id >= 0 && id < 9 ? kNames[id] : "Unknown";
}

// WaypointsWnd: ContinentsList holds one row per category the list uses (ascending ids, std::set), the first one
// selected; WaypointsList that category's waypoints in the list's order, a locked one's name in 0xFFA0A0A0. The
// lists' first columns are 0 wide (the XML); the source puts the name in both columns. Travel to Waypoint sends
// OP_WaypointRequest {the selected row's id, expedition 0, the two check boxes} and closes; Travel To Expedition
// {0, 1, ...} and closes (enabled by the list's expedition flag); a click on either check box sends {0, 0, ...}
// with the boxes' new states. The boxes start as the list says (Port Group enabled with its label by
// group_enabled). Hidden when the game leaves state 5 (OnSetGameState).
class WaypointsWindow : public app::StockWindow {
public:
    WaypointsWindow() : app::StockWindow("WaypointsWnd", false) {}

    bool shown(app::EqHost& host) override {
        NmsExtension* x = ext(host);
        if (!x || !host.in_game) {
            open_ = false;
            return false;
        }
        const auto& wl = x->waypoints();
        if (wl && x->waypoint_serial() != serial_) {
            // HandleWaypointList, then Show.
            serial_ = x->waypoint_serial();
            group_ = wl->group_selected;
            autoconfirm_ = wl->autoconfirm_selected;
            categories_.clear();
            for (const auto& e : wl->entries) categories_.insert(e.category);
            continent_ = 0;
            waypoint_ = -1;
            open_ = true;
        }
        return open_ && wl.has_value();
    }
    const app::ListData* list(app::EqHost& host, std::string_view id) override {
        const WaypointList* list = waypoints(host);
        if (!list) return nullptr;
        const auto& wl = *list;
        data_ = {};
        data_.columns = 2;
        data_.widths = {0, id == "ContinentsList" ? 144 : 320};
        if (id == "ContinentsList") {
            data_.labels = {"", "Continent"};
            for (std::int32_t c : categories_) {
                std::string n = patch_continent_name(c);
                data_.rows.push_back({{n, n}, {1, 1, 1}, std::nullopt});
            }
            data_.selected = continent_;
            return &data_;
        }
        if (id == "WaypointsList") {
            data_.labels = {"", "Waypoint"};
            std::int32_t cat = category();
            for (const auto& e : wl.entries) {
                if (e.category != cat) continue;
                app::ListRow r{{e.name, e.name}, {1, 1, 1}, std::nullopt};
                if (!e.enabled) r.cell_colors = {std::nullopt, app::argb_to_rgb(0xffa0a0a0u)}; // SetItemColor(row, 1, ...)
                data_.rows.push_back(std::move(r));
            }
            data_.selected = waypoint_;
            return &data_;
        }
        return nullptr;
    }
    bool list_click(app::EqHost& host, std::string_view id, int row, bool right, int) override {
        if (!waypoints(host) || right) return false;
        if (id == "ContinentsList") {
            if (row >= 0 && row < static_cast<int>(categories_.size())) {
                continent_ = row;
                waypoint_ = -1; // PopulateWaypoints: DeleteAll
            }
            return true;
        }
        if (id == "WaypointsList") {
            waypoint_ = row;
            return true;
        }
        return false;
    }
    void child(app::EqHost& host, std::string_view id, app::Child& out) override {
        const WaypointList* list = waypoints(host);
        if (!list) return;
        const auto& wl = *list;
        if (id == "GroupCheckbox") out.checked = group_, out.enabled = wl.group_enabled;
        else if (id == "PortGroupLabel") out.enabled = wl.group_enabled;
        else if (id == "AutoAcceptCheckbox") out.checked = autoconfirm_;
        else if (id == "MyExpeditionButton") out.enabled = wl.expedition_enabled;
    }
    bool click(app::EqHost& host, std::string_view id, std::string_view button, bool) override {
        const WaypointList* list = waypoints(host);
        if (!list || button != "left") return false;
        const auto& wl = *list;
        NmsExtension& x = *ext(host);
        WaypointRequest r;
        r.group = group_;
        r.autoconfirm = autoconfirm_;
        if (id == "SelectedWaypointButton") {
            std::int32_t cat = category();
            int n = 0;
            for (const auto& e : wl.entries) {
                if (e.category != cat) continue;
                if (n++ == waypoint_) {
                    r.id = e.id;
                    x.send_waypoint_request(*host.conn, r);
                    open_ = false;
                    break;
                }
            }
        } else if (id == "MyExpeditionButton") {
            if (!wl.expedition_enabled) return true;
            r.expedition = true;
            x.send_waypoint_request(*host.conn, r);
            open_ = false;
        } else if (id == "GroupCheckbox") {
            if (!wl.group_enabled) return true;
            r.group = group_ = !group_;
            x.send_waypoint_request(*host.conn, r);
        } else if (id == "AutoAcceptCheckbox") {
            r.autoconfirm = autoconfirm_ = !autoconfirm_;
            x.send_waypoint_request(*host.conn, r);
        } else {
            return false;
        }
        return true;
    }

private:
    static const WaypointList* waypoints(app::EqHost& host) {
        NmsExtension* x = ext(host);
        return x && x->waypoints() ? &*x->waypoints() : nullptr;
    }
    std::int32_t category() const {
        int n = 0;
        for (std::int32_t c : categories_)
            if (n++ == continent_) return c;
        return -1;
    }

    std::uint32_t serial_ = 0;
    std::set<std::int32_t> categories_;
    int continent_ = 0, waypoint_ = -1;
    bool group_ = false, autoconfirm_ = false;
    app::ListData data_;
};

// rows = eq.NmsCooldowns()   -- the "Autoplay Cooldowns" window's rows (FUN_1000a960): for each of the ten
// auto-skills whose autoskill bit is set and whose classes meet the character's (the class mask, else 1 << (class -
// 1)), {name, left (seconds), fraction (left / the longest seen)}; nil outside NMS mode.
int NmsCooldowns(lua_State* L) {
    auto& host = *static_cast<app::EqHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    NmsExtension* x = ext(host);
    if (!x || !host.in_game) {
        lua_pushnil(L);
        return 1;
    }
    const auto& c = *host.conn;
    std::uint32_t classes = x->class_mask(c);
    if (classes == 0 && c.profile() && c.profile()->class_ > 0) classes = 1u << ((c.profile()->class_ - 1) & 31);
    const auto& cd = x->cooldowns();
    const auto now = static_cast<std::uint32_t>(net::GameConnection::now_ms());
    lua_newtable(L);
    int n = 0;
    for (std::size_t i = 0; i < kAutoSkills.size(); ++i) {
        const auto& s = kAutoSkills[i];
        if (!(cd.autoskill_mask() & (1u << s.bit)) || !(classes & s.class_mask)) continue;
        float left = cd.remaining(i, now);
        lua_createtable(L, 3, 0);
        lua_pushstring(L, s.name);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, left);
        lua_rawseti(L, -2, 2);
        lua_pushnumber(L, left > 0.0f ? left / cd.longest(i) : 0.0f);
        lua_rawseti(L, -2, 3);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

} // namespace

void add_lua_windows(std::vector<std::unique_ptr<app::StockWindow>>& out) {
    out.push_back(std::make_unique<WaypointsWindow>());
}

void register_lua_api(lua_State* L, app::EqHost& host) {
    lua_pushlightuserdata(L, &host);
    lua_pushcclosure(L, NmsCooldowns, 1);
    lua_setfield(L, -2, "NmsCooldowns");
}

} // namespace nmsc::nms
