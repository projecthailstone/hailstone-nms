// The NMS plugin's functions for its Lua windows (ui/<pack>/nms*.lua), called as eq.Plugin("nms").<name>(...). Each
// gives nil before the plugin has seen an NMS server and outside the game, so the windows show nothing then.
//
//   loot()                     the Personal Loot window's state: visible; tab (a tab /loot asked for, once: 0 loot ..
//                              3 settings, else -1); reset (/loot reset, once); rows {pending, item, icon, qty,
//                              charges, value, left (seconds), lore, nodrop, augmented, name, npc, rule (the saved
//                              rule, 0..4)}; settings (nil until the server sent them) {enabled, sellUnder, timeout,
//                              defer, report}; history (newest first) {item, icon, qty, copper, age (seconds), action
//                              (1 kept, 2 sold, 3 destroyed), name, npc}; localHistory (newest first) {name, npc, qty,
//                              rule, age}; rules {id, type, name}; rulesFile
//   loot_send(action [, pending, item, value])   an OP_PersonalLootAction (PersonalLootActionType)
//   loot_rule(item, name, type)  sets (type 1..4) or removes (0) an item rule; saved at once
//   loot_window(op [, value])  "show" (value true / false), "reload" (reread the rules file), "clearHistory", "prefs"
//                              (value {autoOpen, autoClose, pinned, locked, countdown, sound}, the window's settings)
//   vault()                    the Dimensional Pocket Vault (nms_vault.h): shown; page (1..9); labels (the page
//                              buttons); first, last (the page's slots); slots[slot] = {item, icon, name, bag (its
//                              size, 0 not a bag), contents {item, qty, icon, slot, name}}; bags = {{slot, name, size,
//                              contents}} (the open bag windows); cursor = {name, icon} (the item on the cursor) or nil
//   vault_say(command)         a vault button's "/say #vault_..." (HideDoCommand); other text is refused;
//                              "#vault_bank" then opens the bank window with the player as its banker
//   vault_window(op [, slot])  "hide" (the close box), "bag" (a right click on a bag slot: its window opens or
//                              closes), "closeBag", "inspect" (a right click on an item: its display, with its
//                              augments), "inspectItem" (an item id: a bag's item)
//   waypoints()                the waypoint window (the patch's WaypointsWnd): serial (each new list, and /waypoint,
//                              counts it up: the window shows again); groupEnabled, expeditionEnabled, group,
//                              autoconfirm; continents (the patch's names by category, 0-based); entries {category,
//                              id, enabled, name, continent (the server's name), expansion, expansionName, zone}
//   waypoint_request(id, expedition, group, autoconfirm)   OP_WaypointRequest: travel (id), the expedition, or (id 0)
//                              the two check boxes' states
//   pets()                     the Pet window's #petcmd part (nms_petcmd.h): pets (OP_PetList's order) {id, class,
//                              word (the server's class word, "" for none), className, name ("" before the spawn
//                              came), hp (percent), focused (the pet window's)}; verbs {word, label, toggle, swarm};
//                              swarm {hold, ghold, focus, assist} (each absent until the server said)
//   pet_command(verb, state, target)   "/say #petcmd <verb> [on|off] <target>" (all, swarm or a class word); a word
//                              the server does not take is refused; the echo is hidden
//   pet_swarm_query()          asks the swarm pets' settings once a zone (the answers are hidden)

#include <algorithm>
#include <string>

#include "nms_plugin.h"

namespace nms {

namespace {

// ---- the arguments

std::optional<HailstoneValue> arg(const HailstoneArgs* args, std::size_t i) {
    HailstoneValue v{};
    if (!host->arg(host->host, args, i, &v)) return std::nullopt;
    return v;
}

std::int64_t arg_int(const HailstoneArgs* args, std::size_t i, std::int64_t fallback = 0) {
    auto v = arg(args, i);
    if (!v) return fallback;
    if (v->kind == HAILSTONE_INT || v->kind == HAILSTONE_BOOL) return v->i;
    if (v->kind == HAILSTONE_FLOAT) return static_cast<std::int64_t>(v->f);
    return fallback;
}

bool arg_bool(const HailstoneArgs* args, std::size_t i) {
    auto v = arg(args, i);
    if (!v) return false;
    if (v->kind == HAILSTONE_BOOL || v->kind == HAILSTONE_INT) return v->i != 0;
    return v->kind != HAILSTONE_NIL;
}

std::string arg_string(const HailstoneArgs* args, std::size_t i) {
    auto v = arg(args, i);
    return v && v->kind == HAILSTONE_STRING && v->s ? std::string(v->s) : std::string();
}

// A table argument's boolean field, when it has one.
void arg_flag(const HailstoneArgs* args, std::size_t i, const char* key, bool& out) {
    HailstoneValue v{};
    if (host->arg_field(host->host, args, i, key, &v) && (v.kind == HAILSTONE_BOOL || v.kind == HAILSTONE_INT)) out = v.i != 0;
}

// ---- the result

struct Out {
    HailstoneOut* o;
    void table() { host->out_begin_table(host->host, o); }
    void array() { host->out_begin_array(host->host, o); }
    void end() { host->out_end(host->host, o); }
    void key(const char* k) { host->out_key(host->host, o, k); }
    void value(std::int64_t i) {
        HailstoneValue v{};
        v.kind = HAILSTONE_INT;
        v.i = i;
        host->out_value(host->host, o, &v);
    }
    void number(double f) {
        HailstoneValue v{};
        v.kind = HAILSTONE_FLOAT;
        v.f = f;
        host->out_value(host->host, o, &v);
    }
    void flag(bool b) {
        HailstoneValue v{};
        v.kind = HAILSTONE_BOOL;
        v.i = b ? 1 : 0;
        host->out_value(host->host, o, &v);
    }
    void text(const std::string& s) {
        HailstoneValue v{};
        v.kind = HAILSTONE_STRING;
        v.s = s.c_str();
        host->out_value(host->host, o, &v);
    }
    void field(const char* k, std::int64_t i) { key(k), value(i); }
    void field_number(const char* k, double f) { key(k), number(f); }
    void field_flag(const char* k, bool b) { key(k), flag(b); }
    void field_text(const char* k, const std::string& s) { key(k), text(s); }
};

// The plugin when its windows have something to show: an NMS server, in the game.
Nms* active() {
    Nms& n = plugin();
    return n.detected() && in_zone(player()) ? &n : nullptr;
}

// ---- the Personal Loot window

int f_loot(void*, const HailstoneArgs*, HailstoneOut* o) {
    Nms* n = active();
    if (!n) return 0;
    static std::uint32_t reset_seen = 0; // /loot reset as the window last saw it
    PersonalLoot& loot = n->loot;
    const std::int64_t now = now_ms();
    Out out{o};
    out.table();
    out.field_flag("visible", loot.visible());
    out.field("tab", loot.take_requested_tab());
    out.field_flag("reset", reset_seen != n->loot_reset_serial);
    reset_seen = n->loot_reset_serial;
    out.key("rows");
    out.array();
    for (const PersonalLoot::Row* r : loot.visible_rows(now)) {
        out.table();
        out.field("pending", r->d.pending_id);
        out.field("item", r->d.item_id);
        out.field("icon", r->d.icon);
        out.field("qty", r->d.quantity);
        out.field("charges", r->d.charges);
        out.field("value", r->d.sell_value);
        out.field("left", PersonalLoot::seconds_left(*r, now));
        out.field_flag("lore", r->d.lore != 0);
        out.field_flag("nodrop", r->d.no_drop != 0);
        out.field_flag("augmented", r->d.augmented != 0);
        out.field_text("name", r->d.item_name);
        out.field_text("npc", r->d.npc_name);
        out.field("rule", static_cast<int>(loot.rules().get(r->d.item_id)));
        out.end();
    }
    out.end();
    if (const auto& s = loot.settings()) {
        out.key("settings");
        out.table();
        out.field_flag("enabled", (s->flags & 1) != 0);
        out.field("sellUnder", s->sell_under);
        out.field("timeout", s->timeout_seconds);
        out.field_flag("defer", s->defer_in_combat);
        out.field_flag("report", s->report_enabled);
        out.end();
    }
    out.key("history");
    out.array();
    const auto& hist = loot.history();
    const auto since = static_cast<std::uint32_t>(std::max<std::int64_t>(0, now - loot.history_ms()) / 1000);
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
        out.table();
        out.field("item", it->item_id);
        out.field("icon", it->icon);
        out.field("qty", it->quantity);
        out.field("copper", it->copper);
        out.field("age", it->age_seconds + since);
        out.field("action", it->action);
        out.field_text("name", it->item_name);
        out.field_text("npc", it->npc_name);
        out.end();
    }
    out.end();
    out.key("localHistory");
    out.array();
    for (const auto& h : loot.local_history()) {
        out.table();
        out.field_text("name", h.item);
        out.field_text("npc", h.npc);
        out.field("qty", h.quantity);
        out.field("rule", static_cast<int>(h.rule));
        out.field("age", std::max<std::int64_t>(0, now - h.at_ms) / 1000);
        out.end();
    }
    out.end();
    out.key("rules");
    out.array();
    for (const auto& r : loot.rules().rules()) {
        out.table();
        out.field("id", r.item_id);
        out.field("type", static_cast<int>(r.type));
        out.field_text("name", r.name);
        out.end();
    }
    out.end();
    out.field_text("rulesFile", loot.rules().path().string());
    out.end();
    return 1;
}

int f_loot_send(void*, const HailstoneArgs* args, HailstoneOut*) {
    if (Nms* n = active()) {
        LootSend a;
        a.action = static_cast<std::uint32_t>(arg_int(args, 0));
        a.pending_id = static_cast<std::uint32_t>(arg_int(args, 1));
        a.item_id = static_cast<std::uint32_t>(arg_int(args, 2));
        a.value = static_cast<std::uint32_t>(arg_int(args, 3));
        n->send_loot(a);
    }
    return 0;
}

int f_loot_rule(void*, const HailstoneArgs* args, HailstoneOut*) {
    Nms* n = active();
    if (!n) return 0;
    const auto id = static_cast<std::uint32_t>(arg_int(args, 0));
    const auto type = static_cast<int>(arg_int(args, 2, -1));
    if (id == 0 || type < 0 || type >= kLootRuleCount) return 0;
    n->loot.set_rule(id, arg_string(args, 1), static_cast<LootRule>(type));
    return 0;
}

int f_loot_window(void*, const HailstoneArgs* args, HailstoneOut*) {
    Nms* n = active();
    if (!n) return 0;
    PersonalLoot& loot = n->loot;
    const std::string op = arg_string(args, 0);
    if (op == "show") {
        loot.show(arg_bool(args, 1), now_ms());
    } else if (op == "reload") {
        loot.rules().load(loot.rules().path());
    } else if (op == "clearHistory") {
        loot.clear_local_history();
    } else if (op == "prefs") {
        arg_flag(args, 1, "autoOpen", loot.prefs.auto_open);
        arg_flag(args, 1, "autoClose", loot.prefs.auto_close);
        arg_flag(args, 1, "pinned", loot.prefs.pinned);
        arg_flag(args, 1, "locked", loot.prefs.locked);
        arg_flag(args, 1, "countdown", loot.prefs.show_countdown);
        arg_flag(args, 1, "sound", loot.prefs.sound);
    }
    return 0;
}

// ---- the vault

void bag_items(Out& out, const std::vector<VaultBagItem>& items) {
    out.array();
    for (const auto& b : items) {
        out.table();
        out.field("item", b.item_id);
        out.field("qty", b.quantity);
        out.field("icon", b.icon);
        out.field("slot", b.bag_slot);
        out.field_text("name", b.name);
        out.end();
    }
    out.end();
}

int f_vault(void*, const HailstoneArgs*, HailstoneOut* o) {
    Nms* n = active();
    if (!n) return 0;
    const Vault& v = n->vault;
    Out out{o};
    out.table();
    out.field_flag("shown", v.shown());
    out.field("page", v.page());
    out.field("first", vault_page_first(v.page()));
    out.field("last", vault_page_last(v.page()));
    out.key("labels");
    out.array();
    for (int p = 1; p <= 9; ++p) out.text(vault_page_label(p));
    out.end();
    // slots[slot]: a table keyed by the slot number
    out.key("slots");
    out.table();
    for (const auto& s : v.slots()) {
        const std::string slot = std::to_string(s.slot);
        out.key(slot.c_str());
        out.table();
        out.field("item", s.item_id);
        out.field("icon", s.icon);
        out.field_text("name", s.name);
        out.field("bag", s.bag_size);
        out.key("contents");
        bag_items(out, s.contents);
        out.end();
    }
    out.end();
    out.key("bags");
    out.array();
    for (const auto& [slot, b] : v.bags()) {
        if (!b.open) continue;
        out.table();
        out.field("slot", b.slot);
        out.field_text("name", b.name);
        out.field("size", b.size);
        out.key("contents");
        bag_items(out, b.contents);
        out.end();
    }
    out.end();
    if (auto cur = item(HailstoneItemSlot{0, HAILSTONE_SLOT_CURSOR, -1, -1}); cur && cur->name[0]) {
        out.key("cursor");
        out.table();
        out.field_text("name", cur->name);
        out.field("icon", cur->icon);
        out.end();
    }
    out.end();
    return 1;
}

int f_vault_say(void*, const HailstoneArgs* args, HailstoneOut*) {
    if (!active()) return 0;
    const std::string cmd = arg_string(args, 0);
    if (cmd.rfind("#vault_", 0) != 0) return 0;
    command_line("/say " + cmd);
    // 0x1019a420's Bank button: after the say, CBankWnd::Activate(pinstCBankWnd, pLocalPlayer), the bank window
    // with the player as its banker (the server answers the bank moves). A client before open_bank cannot.
    if (cmd == "#vault_bank" && HAILSTONE_HOST_HAS(host, open_bank)) host->open_bank(host->host, 0);
    return 0;
}

int f_vault_window(void*, const HailstoneArgs* args, HailstoneOut*) {
    Nms* n = active();
    if (!n) return 0;
    Vault& v = n->vault;
    const std::string op = arg_string(args, 0);
    const auto slot = static_cast<std::int32_t>(arg_int(args, 1));
    if (op == "hide") {
        v.show(false);
    } else if (op == "bag") {
        if (const VaultSlot* s = v.at(slot)) v.toggle_bag(*s);
    } else if (op == "closeBag") {
        v.close_bag(slot);
    } else if (op == "inspect") {
        // 0x1019c1b0: OP_ItemLinkClick {item, the six augments, flag 4}.
        if (const VaultSlot* s = v.at(slot); s && s->item_id) {
            std::uint32_t augs[6];
            for (std::size_t k = 0; k < 6; ++k) augs[k] = static_cast<std::uint32_t>(s->augments[k]);
            host->inspect_item(host->host, static_cast<std::uint32_t>(s->item_id), augs);
        }
    } else if (op == "inspectItem" && slot > 0) {
        host->inspect_item(host->host, static_cast<std::uint32_t>(slot), nullptr);
    }
    return 0;
}

// ---- the waypoint window

// WaypointsWnd::WaypointsWnd: the continent names by category id (0-based); others are "Unknown". (The patch's own
// table, not the server's categories, nms_waypoints.h continent_name.)
constexpr const char* kPatchContinents[] = {"Antonica", "Faydwer", "Odus",      "Kunark", "Velious",
                                            "Luclin",   "The Planes", "Discord", "Special"};

int f_waypoints(void*, const HailstoneArgs*, HailstoneOut* o) {
    Nms* n = active();
    if (!n || !n->waypoints) return 0;
    const WaypointList& wl = *n->waypoints;
    Out out{o};
    out.table();
    out.field("serial", n->waypoint_serial);
    out.field_flag("groupEnabled", wl.group_enabled);
    out.field_flag("expeditionEnabled", wl.expedition_enabled);
    out.field_flag("group", wl.group_selected);
    out.field_flag("autoconfirm", wl.autoconfirm_selected);
    out.key("continents");
    out.array();
    for (const char* c : kPatchContinents) out.text(c);
    out.end();
    out.key("entries");
    out.array();
    for (const auto& e : wl.entries) {
        out.table();
        out.field("category", e.category);
        out.field("id", e.id);
        out.field_flag("enabled", e.enabled);
        out.field_text("name", e.name);
        out.field_text("continent", continent_name(e.category));
        const std::int32_t x = waypoint_expansion(e);
        out.field("expansion", x);
        out.field_text("expansionName", expansion_name(x));
        out.field_text("zone", std::string(waypoint_zone(e)));
        out.end();
    }
    out.end();
    out.end();
    return 1;
}

int f_waypoint_request(void*, const HailstoneArgs* args, HailstoneOut*) {
    Nms* n = active();
    if (!n || !n->waypoints) return 0;
    WaypointRequest r;
    r.id = static_cast<std::int32_t>(arg_int(args, 0));
    r.expedition = arg_bool(args, 1);
    r.group = arg_bool(args, 2);
    r.autoconfirm = arg_bool(args, 3);
    // The server stores the boxes' states; the list keeps them until its next one.
    n->waypoints->group_selected = r.group;
    n->waypoints->autoconfirm_selected = r.autoconfirm;
    n->send_waypoint_request(r);
    return 0;
}

// ---- the Pet window's #petcmd

int f_pets(void*, const HailstoneArgs*, HailstoneOut* o) {
    Nms* n = active();
    if (!n) return 0;
    const std::uint32_t focused = player().pet_id;
    Out out{o};
    out.table();
    out.key("pets");
    out.array();
    for (const PetListEntry& pet : n->pets()) {
        const auto s = spawn(pet.spawn_id);
        out.table();
        out.field("id", pet.spawn_id);
        out.field("class", pet.class_id);
        out.field_text("word", std::string(pet_class_word(pet.class_id)));
        out.field_text("className", pet_class_name(pet.class_id));
        out.field_text("name", s ? std::string(s->name) : std::string());
        out.field("hp", s ? s->hp_percent : 0);
        out.field_flag("focused", pet.spawn_id == focused);
        out.end();
    }
    out.end();
    out.key("verbs");
    out.array();
    for (const PetVerb& v : pet_verbs()) {
        out.table();
        out.field_text("word", v.word);
        out.field_text("label", v.label);
        out.field_flag("toggle", v.toggle);
        out.field_flag("swarm", v.swarm);
        out.end();
    }
    out.end();
    out.key("swarm");
    out.table();
    for (const auto& [name, on] : n->pet_commands.swarm()) out.field_flag(name.c_str(), on);
    out.end();
    out.end();
    return 1;
}

int f_pet_command(void*, const HailstoneArgs* args, HailstoneOut*) {
    Nms* n = active();
    if (!n) return 0;
    if (auto line = n->pet_commands.command(arg_string(args, 0), arg_string(args, 1), arg_string(args, 2)))
        command_line("/say " + *line);
    return 0;
}

int f_pet_swarm_query(void*, const HailstoneArgs*, HailstoneOut*) {
    Nms* n = active();
    if (!n) return 0;
    if (auto line = n->pet_commands.query_swarm()) command_line("/say " + *line);
    return 0;
}

} // namespace

void register_ui_functions() {
    static const struct {
        const char* name;
        HailstoneUiFn fn;
    } kFunctions[] = {{"loot", f_loot},
                      {"loot_send", f_loot_send},
                      {"loot_rule", f_loot_rule},
                      {"loot_window", f_loot_window},
                      {"vault", f_vault},
                      {"vault_say", f_vault_say},
                      {"vault_window", f_vault_window},
                      {"waypoints", f_waypoints},
                      {"waypoint_request", f_waypoint_request},
                      {"pets", f_pets},
                      {"pet_command", f_pet_command},
                      {"pet_swarm_query", f_pet_swarm_query}};
    for (const auto& f : kFunctions) host->add_ui_function(host->host, f.name, f.fn, nullptr);
}

} // namespace nms
