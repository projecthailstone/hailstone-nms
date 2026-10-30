// The NMS patch's Personal Loot window without its drawing (NMS-Release eqgame_dll/loot_window.cpp; see nms_loot.h).

#include "nms_loot.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <system_error>

#include "nms_ini.h"

namespace nms {

// ---------------------------------------------------------------- the rules

const char* loot_rule_section(LootRule r) {
    static const char* const kSection[kLootRuleCount] = {"", "AlwaysKeep", "AlwaysSell", "KeepOnce", "SellOnce"};
    return kSection[static_cast<int>(r)];
}

const char* loot_rule_label(LootRule r) {
    static const char* const kLabel[kLootRuleCount] = {"-", "Always Keep", "Always Sell", "Keep Once", "Sell Once"};
    return kLabel[static_cast<int>(r)];
}

void LootRules::load(const std::filesystem::path& path) {
    // LoadRules: every section in rule order, the last section an id appears in wins.
    path_ = path;
    rules_.clear();
    if (path_.empty()) return;
    Ini ini;
    ini.load(path_);
    for (int t = 1; t < kLootRuleCount; ++t) {
        const LootRule type = static_cast<LootRule>(t);
        for (const std::string& key : ini.keys(loot_rule_section(type))) {
            LootItemRule r;
            r.item_id = static_cast<std::uint32_t>(std::atoi(key.c_str()));
            r.type = type;
            r.name = ini.get_string(loot_rule_section(type), key);
            if (!r.item_id) continue;
            auto it = std::find_if(rules_.begin(), rules_.end(), [&](const auto& e) { return e.item_id == r.item_id; });
            if (it != rules_.end()) *it = std::move(r);
            else rules_.push_back(std::move(r));
        }
    }
}

void LootRules::save() const {
    // SaveRules: each section rewritten whole, in the rules' order.
    if (path_.empty()) return;
    Ini ini;
    for (int t = 1; t < kLootRuleCount; ++t) {
        const LootRule type = static_cast<LootRule>(t);
        for (const auto& r : rules_)
            if (r.type == type) ini.set(loot_rule_section(type), std::to_string(r.item_id), r.name);
    }
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    ini.save(path_);
}

LootRule LootRules::get(std::uint32_t item_id) const {
    for (const auto& r : rules_)
        if (r.item_id == item_id) return r.type;
    return LootRule::None;
}

void LootRules::set(std::uint32_t item_id, const std::string& name, LootRule type) {
    // SetRule: None erases; an existing rule changes type (and name unless empty); a new one is appended.
    auto it = std::find_if(rules_.begin(), rules_.end(), [&](const auto& e) { return e.item_id == item_id; });
    if (it != rules_.end()) {
        if (type == LootRule::None) rules_.erase(it);
        else {
            it->type = type;
            if (!name.empty()) it->name = name;
        }
        save();
        return;
    }
    if (type == LootRule::None) return;
    rules_.push_back({item_id, type, name});
    save();
}

void LootRules::clear() {
    rules_.clear();
    save();
}

void LootRules::drop(std::uint32_t item_id) {
    std::erase_if(rules_, [&](const LootItemRule& e) { return e.item_id == item_id; });
}

void LootRules::forget() {
    rules_.clear();
    path_.clear();
}

// ---------------------------------------------------------------- the window

bool PersonalLoot::auto_resolving(std::uint32_t pending_id, std::int64_t now_ms) {
    // IsAutoResolving: a row the server did not drop within 10 s (lore conflict, full inventory ...) shows again.
    bool found = false;
    std::erase_if(auto_resolving_, [&](const AutoResolve& a) { return now_ms - a.at_ms > 10000; });
    for (const auto& a : auto_resolving_)
        if (a.pending_id == pending_id) found = true;
    return found;
}

std::vector<const PersonalLoot::Row*> PersonalLoot::visible_rows(std::int64_t now_ms) {
    std::vector<const Row*> out;
    for (const Row& r : rows_)
        if (!auto_resolving(r.d.pending_id, now_ms)) out.push_back(&r);
    return out;
}

std::size_t PersonalLoot::visible_count(std::int64_t now_ms) { return visible_rows(now_ms).size(); }

std::uint32_t PersonalLoot::seconds_left(const Row& r, std::int64_t now_ms) {
    const auto elapsed = static_cast<std::uint32_t>(std::max<std::int64_t>(0, now_ms - r.received_ms) / 1000);
    return r.d.seconds_left > elapsed ? r.d.seconds_left - elapsed : 0;
}

std::vector<LootSend> PersonalLoot::on_list(const PersonalLootList& list, std::int64_t now_ms) {
    std::vector<LootSend> sends;
    if (list.flags & kLootListCamping) {
        visible_ = forced_open_ = false;
        return sends;
    }
    const std::size_t before = visible_count(now_ms);
    std::vector<Row> rows;
    std::vector<std::uint32_t> new_ids;
    for (const auto& d : list.rows) {
        bool known = false;
        for (const Row& old : rows_)
            if (old.d.pending_id == d.pending_id) {
                known = old.d.quantity == d.quantity;
                break;
            }
        if (!known) new_ids.push_back(d.pending_id);
        rows.push_back({d, now_ms});
    }
    rows_.swap(rows);
    // Rows the server dropped are no longer being answered.
    std::erase_if(auto_resolving_, [&](const AutoResolve& a) {
        return std::none_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.d.pending_id == a.pending_id; });
    });
    // ApplyRules: answer every row a rule covers before deciding whether to bother the player.
    bool rules_changed = false;
    for (const Row& r : rows_) {
        if (auto_resolving(r.d.pending_id, now_ms)) continue;
        const LootRule t = rules_.get(r.d.item_id);
        if (t == LootRule::None) continue;
        sends.push_back({loot_rule_keeps(t) ? kLootActionKeep : kLootActionSell, r.d.pending_id, 0, 0});
        auto_resolving_.push_back({r.d.pending_id, now_ms});
        local_history_.insert(local_history_.begin(), {r.d.item_name, r.d.npc_name, r.d.quantity, t, now_ms});
        if (local_history_.size() > 100) local_history_.pop_back();
        if (t == LootRule::KeepOnce || t == LootRule::SellOnce) {
            rules_.drop(r.d.item_id);
            rules_changed = true;
        }
    }
    if (rules_changed) rules_.save();

    const std::size_t visible = visible_count(now_ms);
    bool new_visible = false;
    for (std::uint32_t id : new_ids)
        if (!auto_resolving(id, now_ms)) new_visible = true;
    if ((list.flags & kLootListShow) && (visible > 0 || !visible_)) {
        forced_open_ = visible == 0;
        visible_ = true;
    }
    if (visible > 0) {
        forced_open_ = false;
        if (new_visible) {
            if (prefs.auto_open && !visible_) {
                visible_ = true;
                requested_tab_ = 0;
            }
            if (visible > before && prefs.sound) beep_ = true;
        }
    } else if (before > 0 && prefs.auto_close && !prefs.pinned && !forced_open_) {
        visible_ = false;
    }
    return sends;
}

void PersonalLoot::on_history(std::vector<LootHistoryRow> h, std::int64_t now_ms) {
    history_ = std::move(h);
    history_ms_ = now_ms;
}

std::optional<LootSend> PersonalLoot::pulse(bool player_ready, std::int64_t now_ms) {
    // OnPulse: introduce ourselves once per zone, a few seconds after zone-in so CAuth is done first.
    if (hello_sent_ || !player_ready) return std::nullopt;
    if (hello_due_ms_ == 0) {
        hello_due_ms_ = now_ms + 3000;
        return std::nullopt;
    }
    if (now_ms < hello_due_ms_) return std::nullopt;
    hello_sent_ = true;
    return LootSend{kLootActionHello, 0, 0, 0};
}

void PersonalLoot::reset(bool left_world) {
    // OnSetGameState for a state other than in game; a zone change re-hellos as well.
    visible_ = forced_open_ = false;
    hello_sent_ = false;
    hello_due_ms_ = 0;
    rows_.clear();
    auto_resolving_.clear();
    settings_.reset();
    if (left_world) rules_.forget();
}

void PersonalLoot::show(bool show, std::int64_t now_ms) {
    visible_ = show;
    forced_open_ = show && visible_count(now_ms) == 0;
}

std::string loot_money(std::uint32_t copper) {
    if (copper == 0) return "-";
    const std::uint32_t pp = copper / 1000, gp = (copper / 100) % 10, sp = (copper / 10) % 10, cp = copper % 10;
    std::string s;
    if (pp) s += std::format("{}p ", pp);
    if (gp) s += std::format("{}g ", gp);
    if (sp) s += std::format("{}s ", sp);
    if (cp || s.empty()) s += std::format("{}c", cp);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---------------------------------------------------------------- /loot

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// MQ2's GetArg: whitespace-separated, "quoted" arguments keep their spaces.
std::vector<std::string> args_of(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        std::string a;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') a += line[i++];
            if (i < line.size()) ++i;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') a += line[i++];
        }
        out.push_back(std::move(a));
    }
    return out;
}

} // namespace

LootCommandResult loot_command(PersonalLoot& loot, const std::string& line, std::int64_t now_ms) {
    LootCommandResult res;
    const auto args = args_of(line);
    const std::string arg = args.empty() ? "" : lower(args[0]);
    const std::string arg2 = args.size() > 1 ? lower(args[1]) : "";
    auto say = [&](std::string s) { res.lines.push_back(std::move(s)); };
    if (arg.empty() || arg == "toggle") {
        loot.toggle(now_ms);
    } else if (arg == "show" || arg == "open") {
        loot.show(true, now_ms);
        res.sends.push_back({kLootActionRefresh, 0, 0, 0});
    } else if (arg == "hide" || arg == "close") {
        loot.show(false, now_ms);
    } else if (arg == "pin" || arg == "unpin") {
        loot.prefs.pinned = arg == "pin";
        say(loot.prefs.pinned ? "[Loot] Window pinned." : "[Loot] Window unpinned.");
    } else if (arg == "lock" || arg == "unlock") {
        loot.prefs.locked = arg == "lock";
        say(loot.prefs.locked ? "[Loot] Window locked." : "[Loot] Window unlocked.");
    } else if (arg == "tab") {
        if (arg2 == "loot") loot.request_tab(0);
        else if (arg2 == "rules") loot.request_tab(1);
        else if (arg2 == "history") loot.request_tab(2);
        else if (arg2 == "settings") loot.request_tab(3);
        loot.show(true, now_ms);
    } else if (arg == "reset") {
        res.reset_window = true;
        loot.show(true, now_ms);
    } else if (arg == "refresh") {
        res.sends.push_back({kLootActionHello, 0, 0, 0});
    } else if (arg == "rules") {
        if (arg2 == "clear") {
            loot.rules().clear();
            say(std::format("[Loot] All rules cleared ({}).", loot.rules().path().string()));
        } else {
            loot.rules().load(loot.rules().path());
            say(std::format("[Loot] Reloaded {} rules from {}", loot.rules().rules().size(),
                            loot.rules().path().string()));
        }
    } else if (arg == "rule") {
        const std::string arg3 = args.size() > 2 ? lower(args[2]) : "";
        const auto id = static_cast<std::uint32_t>(std::atoi(arg2.c_str()));
        LootRule t = LootRule::None;
        bool ok = id > 0;
        if (arg3 == "keep" || arg3 == "alwayskeep") t = LootRule::AlwaysKeep;
        else if (arg3 == "sell" || arg3 == "alwayssell") t = LootRule::AlwaysSell;
        else if (arg3 == "keeponce") t = LootRule::KeepOnce;
        else if (arg3 == "sellonce") t = LootRule::SellOnce;
        else if (arg3 != "none" && arg3 != "remove") ok = false;
        if (!ok) {
            say("[Loot] /loot rule <item id> <keep|sell|keeponce|sellonce|none>");
            return res;
        }
        std::string name; // keep the name if a pending row has it
        for (const auto& r : loot.rows())
            if (r.d.item_id == id) {
                name = r.d.item_name;
                break;
            }
        loot.set_rule(id, name, t);
        say(std::format("[Loot] Item {}: {}", id, loot_rule_label(t)));
    } else if (arg == "lootall") {
        res.sends.push_back({kLootActionLootAll, 0, 0, 0});
    } else if (arg == "sellall") {
        res.sends.push_back({kLootActionSellAll, 0, 0, 0});
    } else {
        // scale / alpha / debug / demo are the patch's overlay controls; this client's UI scale is its own.
        say("[Loot] /loot [toggle|show|hide|pin|unpin|lock|unlock|tab <loot|rules|history|settings>|rule <id> "
            "<keep|sell|keeponce|sellonce|none>|rules [clear]|reset|refresh|lootall|sellall]");
    }
    return res;
}

} // namespace nms
