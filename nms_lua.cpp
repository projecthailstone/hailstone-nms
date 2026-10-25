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

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <lua.hpp>

#include "app/lua_eq.h"
#include "app/lua_eq_windows.h"
#include "game/item_links.h"
#include "net/game_connection.h"
#include "plugins/nms/nms_extension.h"

namespace nmsc::nms {

namespace {

// /loot reset as the Lua window last saw it.
std::uint32_t& loot_reset_seen() {
    static std::uint32_t seen = 0;
    return seen;
}

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

// ---------------------------------------------------------------- the Personal Loot window (plugins/nms/nms_loot.h)

void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_str(lua_State* L, const char* k, const std::string& v) {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}
app::EqHost& host_of(lua_State* L) { return *static_cast<app::EqHost*>(lua_touserdata(L, lua_upvalueindex(1))); }
NmsExtension* loot_ext(lua_State* L) {
    app::EqHost& host = host_of(L);
    return host.in_game ? ext(host) : nullptr;
}

// st = eq.NmsLoot()   -- the Personal Loot window's state; nil outside NMS mode. st.visible; st.tab (a tab /loot
// asked for, once: 0 loot .. 3 settings, else -1); st.reset (/loot reset, once); st.rows {pending, item, icon, qty,
// charges, value, left (seconds), lore, nodrop, augmented, name, npc, rule (the saved rule, 0..4)}; st.settings
// (nil until the server sent them) {enabled, sellUnder, timeout, defer, report}; st.history (newest first) {item,
// icon, qty, copper, age (seconds), action (1 kept, 2 sold, 3 destroyed), name, npc}; st.localHistory (newest
// first) {name, npc, qty, rule, age}; st.rules {id, type, name}; st.rulesFile.
int NmsLoot(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) {
        lua_pushnil(L);
        return 1;
    }
    PersonalLoot& loot = x->personal_loot();
    const std::int64_t now = net::GameConnection::now_ms();
    lua_newtable(L);
    set_bool(L, "visible", loot.visible());
    set_int(L, "tab", loot.take_requested_tab());
    auto& seen = loot_reset_seen();
    set_bool(L, "reset", seen != x->loot_reset_serial());
    seen = x->loot_reset_serial();
    lua_newtable(L);
    int n = 0;
    for (const PersonalLoot::Row* r : loot.visible_rows(now)) {
        lua_newtable(L);
        set_int(L, "pending", r->d.pending_id);
        set_int(L, "item", r->d.item_id);
        set_int(L, "icon", r->d.icon);
        set_int(L, "qty", r->d.quantity);
        set_int(L, "charges", r->d.charges);
        set_int(L, "value", r->d.sell_value);
        set_int(L, "left", PersonalLoot::seconds_left(*r, now));
        set_bool(L, "lore", r->d.lore != 0);
        set_bool(L, "nodrop", r->d.no_drop != 0);
        set_bool(L, "augmented", r->d.augmented != 0);
        set_str(L, "name", r->d.item_name);
        set_str(L, "npc", r->d.npc_name);
        set_int(L, "rule", static_cast<int>(loot.rules().get(r->d.item_id)));
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "rows");
    if (const auto& s = loot.settings()) {
        lua_newtable(L);
        set_bool(L, "enabled", (s->flags & 1) != 0);
        set_int(L, "sellUnder", s->sell_under);
        set_int(L, "timeout", s->timeout_seconds);
        set_bool(L, "defer", s->defer_in_combat);
        set_bool(L, "report", s->report_enabled);
        lua_setfield(L, -2, "settings");
    }
    lua_newtable(L);
    n = 0;
    const auto& hist = loot.history();
    const auto since = static_cast<std::uint32_t>(std::max<std::int64_t>(0, now - loot.history_ms()) / 1000);
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
        lua_newtable(L);
        set_int(L, "item", it->item_id);
        set_int(L, "icon", it->icon);
        set_int(L, "qty", it->quantity);
        set_int(L, "copper", it->copper);
        set_int(L, "age", it->age_seconds + since);
        set_int(L, "action", it->action);
        set_str(L, "name", it->item_name);
        set_str(L, "npc", it->npc_name);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "history");
    lua_newtable(L);
    n = 0;
    for (const auto& h : loot.local_history()) {
        lua_newtable(L);
        set_str(L, "name", h.item);
        set_str(L, "npc", h.npc);
        set_int(L, "qty", h.quantity);
        set_int(L, "rule", static_cast<int>(h.rule));
        set_int(L, "age", std::max<std::int64_t>(0, now - h.at_ms) / 1000);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "localHistory");
    lua_newtable(L);
    n = 0;
    for (const auto& r : loot.rules().rules()) {
        lua_newtable(L);
        set_int(L, "id", r.item_id);
        set_int(L, "type", static_cast<int>(r.type));
        set_str(L, "name", r.name);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "rules");
    set_str(L, "rulesFile", loot.rules().path().string());
    return 1;
}

// eq.NmsLootSend(action [, pending, item, value])   -- an OP_PersonalLootAction (PersonalLootActionType)
int NmsLootSend(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) return 0;
    LootSend a;
    a.action = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    a.pending_id = static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0));
    a.item_id = static_cast<std::uint32_t>(luaL_optinteger(L, 3, 0));
    a.value = static_cast<std::uint32_t>(luaL_optinteger(L, 4, 0));
    x->send_loot(*host_of(L).conn, a);
    return 0;
}

// eq.NmsLootRule(item, name, type)   -- sets (type 1..4) or removes (0) an item rule; saved at once
int NmsLootRule(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) return 0;
    const auto id = static_cast<std::uint32_t>(luaL_checkinteger(L, 1));
    const char* name = luaL_optstring(L, 2, "");
    const auto type = static_cast<int>(luaL_checkinteger(L, 3));
    if (id == 0 || type < 0 || type >= kLootRuleCount) return 0;
    x->personal_loot().set_rule(id, name, static_cast<LootRule>(type));
    return 0;
}

// eq.NmsLootWindow(op [, value])   -- "show" (value: true / false), "reload" (reread the rules file), "clearHistory",
// "prefs" (value: {autoOpen, autoClose, pinned, locked, countdown, sound}, the window's settings)
int NmsLootWindow(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) return 0;
    PersonalLoot& loot = x->personal_loot();
    const std::string op = luaL_checkstring(L, 1);
    if (op == "show") {
        loot.show(lua_toboolean(L, 2) != 0, net::GameConnection::now_ms());
    } else if (op == "reload") {
        loot.rules().load(loot.rules().path());
    } else if (op == "clearHistory") {
        loot.clear_local_history();
    } else if (op == "prefs" && lua_istable(L, 2)) {
        auto flag = [&](const char* k, bool& v) {
            lua_getfield(L, 2, k);
            if (!lua_isnil(L, -1)) v = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        };
        flag("autoOpen", loot.prefs.auto_open);
        flag("autoClose", loot.prefs.auto_close);
        flag("pinned", loot.prefs.pinned);
        flag("locked", loot.prefs.locked);
        flag("countdown", loot.prefs.show_countdown);
        flag("sound", loot.prefs.sound);
    }
    return 0;
}

// ---------------------------------------------------------------- the Multiclass Autoplay Console (FUN_1000a430)

// st = eq.NmsAutoplay()   -- nil outside NMS mode. st.console: the console shown (nil until the UI gave its saved
// state); st.rows: for each of the ten auto-skills whose classes meet the character's (the class mask, else
// 1 << (class - 1)), {name, enabled (its autoskill bit), left (seconds), fraction (left / the longest seen)}.
int NmsAutoplay(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) {
        lua_pushnil(L);
        return 1;
    }
    const auto& c = *host_of(L).conn;
    std::uint32_t classes = x->class_mask(c);
    if (classes == 0 && c.profile() && c.profile()->class_ > 0) classes = 1u << ((c.profile()->class_ - 1) & 31);
    const auto& cd = x->cooldowns();
    const auto now = static_cast<std::uint32_t>(net::GameConnection::now_ms());
    lua_newtable(L);
    if (x->autoplay_console) set_bool(L, "console", *x->autoplay_console);
    lua_newtable(L);
    int n = 0;
    for (std::size_t i = 0; i < kAutoSkills.size(); ++i) {
        const auto& s = kAutoSkills[i];
        if (!(classes & s.class_mask)) continue;
        const float left = cd.remaining(i, now);
        lua_newtable(L);
        set_str(L, "name", s.name);
        set_bool(L, "enabled", (cd.autoskill_mask() & (1u << s.bit)) != 0);
        lua_pushnumber(L, left);
        lua_setfield(L, -2, "left");
        lua_pushnumber(L, left > 0.0f ? left / cd.longest(i) : 0.0f);
        lua_setfield(L, -2, "fraction");
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "rows");
    return 1;
}

// eq.NmsAutoplayConsole(shown)   -- shows or hides the console (the UI's saved state, or its close box)
int NmsAutoplayConsole(lua_State* L) {
    if (NmsExtension* x = loot_ext(L)) x->autoplay_console = lua_toboolean(L, 1) != 0;
    return 0;
}

// eq.NmsAutoskill(name, enable)   -- a console check box: "/say #autoskill <name> enable|disable" (HideDoCommand)
int NmsAutoskill(lua_State* L) {
    if (!loot_ext(L)) return 0;
    const std::string name = luaL_checkstring(L, 1);
    host_of(L).conn->send_command("/say #autoskill " + name + (lua_toboolean(L, 2) ? " enable" : " disable"));
    return 0;
}

// ---------------------------------------------------------------- the Dimensional Pocket Vault (plugins/nms/nms_vault.h)

void push_bag_items(lua_State* L, const std::vector<VaultBagItem>& items) {
    lua_newtable(L);
    int n = 0;
    for (const auto& b : items) {
        lua_newtable(L);
        set_int(L, "item", b.item_id);
        set_int(L, "qty", b.quantity);
        set_int(L, "icon", b.icon);
        set_int(L, "slot", b.bag_slot);
        set_str(L, "name", b.name);
        lua_rawseti(L, -2, ++n);
    }
}

// v = eq.NmsVault()   -- nil outside NMS mode. v.shown; v.page (1..9); v.labels (the page buttons); v.first, v.last
// (the page's slots); v.slots[slot] = {item, icon, name, bag (its size, 0 not a bag), contents {item, qty, icon,
// slot, name}}; v.bags = {{slot, name, size, contents}} (the open bag windows); v.cursor = {name, icon} (the item on
// the cursor) or nil.
int NmsVault(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) {
        lua_pushnil(L);
        return 1;
    }
    const Vault& v = x->vault();
    lua_newtable(L);
    set_bool(L, "shown", v.shown());
    set_int(L, "page", v.page());
    set_int(L, "first", vault_page_first(v.page()));
    set_int(L, "last", vault_page_last(v.page()));
    lua_newtable(L);
    for (int p = 1; p <= 9; ++p) {
        lua_pushstring(L, vault_page_label(p));
        lua_rawseti(L, -2, p);
    }
    lua_setfield(L, -2, "labels");
    lua_newtable(L);
    for (const auto& s : v.slots()) {
        lua_newtable(L);
        set_int(L, "item", s.item_id);
        set_int(L, "icon", s.icon);
        set_str(L, "name", s.name);
        set_int(L, "bag", s.bag_size);
        push_bag_items(L, s.contents);
        lua_setfield(L, -2, "contents");
        lua_rawseti(L, -2, s.slot);
    }
    lua_setfield(L, -2, "slots");
    lua_newtable(L);
    int n = 0;
    for (const auto& [slot, b] : v.bags()) {
        if (!b.open) continue;
        lua_newtable(L);
        set_int(L, "slot", b.slot);
        set_str(L, "name", b.name);
        set_int(L, "size", b.size);
        push_bag_items(L, b.contents);
        lua_setfield(L, -2, "contents");
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "bags");
    const auto& conn = *host_of(L).conn;
    if (const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(net::kSlotCursor)); it && !it->name.empty()) {
        lua_newtable(L);
        set_str(L, "name", it->name);
        set_int(L, "icon", it->icon);
        lua_setfield(L, -2, "cursor");
    }
    return 1;
}

// eq.NmsVaultSay(command)   -- a vault button's "/say #vault_..." (HideDoCommand); other text is refused
int NmsVaultSay(lua_State* L) {
    if (!loot_ext(L)) return 0;
    const std::string cmd = luaL_checkstring(L, 1);
    if (cmd.rfind("#vault_", 0) != 0) return 0;
    host_of(L).conn->send_command("/say " + cmd);
    return 0;
}

// eq.NmsVaultWindow(op [, slot])   -- "hide" (the close box), "bag" (a right click on a bag slot: its window opens or
// closes), "closeBag", "inspect" (a right click on an item: OP_ItemLinkClick with its augments), "inspectItem" (an
// item id: a bag's item)
int NmsVaultWindow(lua_State* L) {
    NmsExtension* x = loot_ext(L);
    if (!x) return 0;
    Vault& v = x->vault();
    const std::string op = luaL_checkstring(L, 1);
    const auto arg = static_cast<std::int32_t>(luaL_optinteger(L, 2, 0));
    if (op == "hide") {
        v.show(false);
    } else if (op == "bag") {
        if (const VaultSlot* s = v.at(arg)) v.toggle_bag(*s);
    } else if (op == "closeBag") {
        v.close_bag(arg);
    } else if (op == "inspect") {
        // FUN_1019c1b0: OP_ItemLinkClick {item, the six augments, flag 4}.
        if (const VaultSlot* s = v.at(arg); s && s->item_id) {
            game::ItemLink link;
            link.item_id = static_cast<std::uint32_t>(s->item_id);
            for (std::size_t k = 0; k < 6; ++k) link.augments[k] = static_cast<std::uint32_t>(s->augments[k]);
            host_of(L).conn->request_item(link);
        }
    } else if (op == "inspectItem" && arg > 0) {
        game::ItemLink link;
        link.item_id = static_cast<std::uint32_t>(arg);
        host_of(L).conn->request_item(link);
    }
    return 0;
}

} // namespace

void add_lua_windows(std::vector<std::unique_ptr<app::StockWindow>>& out) {
    out.push_back(std::make_unique<WaypointsWindow>());
}

void register_lua_api(lua_State* L, app::EqHost& host) {
    static const luaL_Reg kFns[] = {{"NmsCooldowns", NmsCooldowns},   {"NmsLoot", NmsLoot},
                                    {"NmsLootSend", NmsLootSend},     {"NmsLootRule", NmsLootRule},
                                    {"NmsLootWindow", NmsLootWindow}, {"NmsAutoplay", NmsAutoplay},
                                    {"NmsAutoplayConsole", NmsAutoplayConsole},
                                    {"NmsAutoskill", NmsAutoskill},   {"NmsVault", NmsVault},
                                    {"NmsVaultSay", NmsVaultSay},     {"NmsVaultWindow", NmsVaultWindow},
                                    {nullptr, nullptr}};
    for (const luaL_Reg* f = kFns; f->name; ++f) {
        lua_pushlightuserdata(L, &host);
        lua_pushcclosure(L, f->func, 1);
        lua_setfield(L, -2, f->name);
    }
}

} // namespace nmsc::nms
