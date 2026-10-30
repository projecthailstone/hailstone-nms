#pragma once

// The NMS client patch's Personal Loot window, the client side of the server's personal loot (Custom:Loot_Enabled,
// zone/loot_distribution.cpp): NPC corpses spawn empty and each player decides Keep / Sell for his own roll of the
// drops. The patch is NMS-Release Release-NMS-Client/eqgame_dll/loot_window.cpp (the dinput8.dll the NMS-Release
// server ships in ClientFiles/); this is its state without the drawing, which the UI packs' nmsloot.lua does over
// eq.NmsLoot* (plugins/nms/nms_lua.cpp):
//   - the server's snapshots: OP_PersonalLootList (net/item_packets.h), OP_PersonalLootFilters, OP_PersonalLootSettings
//     and OP_PersonalLootHistory (below);
//   - the item rules (Always Keep / Always Sell / Keep Once / Sell Once) in the character's itemrules.ini (the
//     patch's <charname>_itemrules.ini next to eqgame.exe; here in the character's plugin-config/nms/), answered
//     by the client before the window opens;
//   - the window's own preferences (the patch's NMS_loot.ini; here the UI's settings, nms.loot.*);
//   - the Hello (OP_PersonalLootAction 20) once per zone, 3 s after the player exists, which makes the server send
//     the settings, filters, triage list and history.
// The server only offers the window to clients it authorised (OP_CAuth, Client::IsClientDllAuthorized); others get
// its two-button popup per item.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "nms_packets.h"

namespace nms {

// OP_PersonalLootList's flags.
inline constexpr std::uint32_t kLootListShow = 1, kLootListCamping = 2;

// ---- the item rules ----
enum class LootRule : int { None = 0, AlwaysKeep, AlwaysSell, KeepOnce, SellOnce };
inline constexpr int kLootRuleCount = 5;
// The ini sections ("AlwaysKeep", ...) and the window's labels ("Always Keep", ...); None: "" and "-".
const char* loot_rule_section(LootRule r);
const char* loot_rule_label(LootRule r);
inline bool loot_rule_keeps(LootRule r) { return r == LootRule::AlwaysKeep || r == LootRule::KeepOnce; }

struct LootItemRule {
    std::uint32_t item_id = 0;
    LootRule type = LootRule::None;
    std::string name; // only so the file reads by hand
};

// The rules file: one section per rule type, "item_id=Item Name" per line; an id in two sections takes the last.
class LootRules {
public:
    // Reads `path` (no file: no rules). An empty path drops the rules and saves nothing.
    void load(const std::filesystem::path& path);
    void save() const;
    const std::filesystem::path& path() const { return path_; }
    const std::vector<LootItemRule>& rules() const { return rules_; }
    LootRule get(std::uint32_t item_id) const;
    // None removes the rule; a new name replaces the stored one unless empty. Saves.
    void set(std::uint32_t item_id, const std::string& name, LootRule type);
    void clear();
    void drop(std::uint32_t item_id); // removes the rule without saving (a Once rule that fired)
    void forget(); // leaves the character: no rules, no file

private:
    std::filesystem::path path_;
    std::vector<LootItemRule> rules_;
};

// ---- the window ----
struct LootPrefs {
    bool auto_open = true;      // open when loot arrives
    bool auto_close = true;     // close when the last row resolves
    bool pinned = false;        // never auto-close
    bool locked = false;        // no move / resize
    bool show_countdown = true; // the Time column
    bool sound = true;          // a beep when new rows arrive
};

// What the rules or the player did, for the History tab (the patch's LocalHistory), newest first, at most 100.
struct LootLocalHistory {
    std::string item, npc;
    std::uint32_t quantity = 0;
    LootRule rule = LootRule::None;
    std::int64_t at_ms = 0;
};

// An OP_PersonalLootAction to send: {action, pending id, item id, value}.
struct LootSend {
    std::uint32_t action = 0, pending_id = 0, item_id = 0, value = 0;
    bool operator==(const LootSend&) const = default;
};

class PersonalLoot {
public:
    struct Row {
        PersonalLootRow d;
        std::int64_t received_ms = 0; // for the countdown
    };

    // OnList: takes the snapshot, answers every row a rule covers (returned, to send), and opens or closes the
    // window as the patch does. Camping hides it.
    std::vector<LootSend> on_list(const PersonalLootList& list, std::int64_t now_ms);
    void on_settings(const LootSettings& s) { settings_ = s; }
    void on_history(std::vector<LootHistoryRow> h, std::int64_t now_ms);
    // OnPulse: the Hello once per zone, 3 s after the player exists.
    std::optional<LootSend> pulse(bool player_ready, std::int64_t now_ms);
    // OnSetGameState: in the game again (a new zone) re-arms the Hello; leaving it hides the window and drops the
    // rows and settings, and leaving the world (`left_world`) forgets the character's rules too.
    void reset(bool left_world);

    // The rows not being answered by a rule (a row answered more than 10 s ago shows again: the server kept it).
    std::vector<const Row*> visible_rows(std::int64_t now_ms);
    std::size_t visible_count(std::int64_t now_ms);
    static std::uint32_t seconds_left(const Row& r, std::int64_t now_ms);

    // The player's buttons and menus (they also keep the rules).
    void set_rule(std::uint32_t item_id, const std::string& name, LootRule type) { rules_.set(item_id, name, type); }
    void show(bool show, std::int64_t now_ms);
    void toggle(std::int64_t now_ms) { show(!visible_, now_ms); }
    void request_tab(int tab) { requested_tab_ = tab; }
    int take_requested_tab() { int t = requested_tab_; requested_tab_ = -1; return t; }
    void clear_local_history() { local_history_.clear(); }
    // A new visible row arrived (the window beeps once per such snapshot); cleared when read.
    bool take_beep() { bool b = beep_; beep_ = false; return b; }

    bool visible() const { return visible_; }
    bool forced_open() const { return forced_open_; }
    const std::vector<Row>& rows() const { return rows_; }
    const std::optional<LootSettings>& settings() const { return settings_; }
    const std::vector<LootHistoryRow>& history() const { return history_; }
    std::int64_t history_ms() const { return history_ms_; }
    const std::vector<LootLocalHistory>& local_history() const { return local_history_; }
    LootRules& rules() { return rules_; }
    const LootRules& rules() const { return rules_; }
    LootPrefs prefs;

private:
    bool auto_resolving(std::uint32_t pending_id, std::int64_t now_ms);

    std::vector<Row> rows_;
    std::vector<LootHistoryRow> history_;
    std::int64_t history_ms_ = 0;
    std::optional<LootSettings> settings_;
    LootRules rules_;
    struct AutoResolve {
        std::uint32_t pending_id;
        std::int64_t at_ms;
    };
    std::vector<AutoResolve> auto_resolving_;
    std::vector<LootLocalHistory> local_history_;
    bool visible_ = false, forced_open_ = false, beep_ = false;
    bool hello_sent_ = false;
    std::int64_t hello_due_ms_ = 0;
    int requested_tab_ = -1; // 0 loot, 1 rules, 2 history, 3 settings
};

// The patch's coin text: "1p 2g 3s 4c" (zero parts left out; "-" for 0).
std::string loot_money(std::uint32_t copper);

// /loot (and /nmsloot): the patch's LootCommand. Returns the chat lines; `send` gets the actions to send.
struct LootCommandResult {
    std::vector<std::string> lines;
    std::vector<LootSend> sends;
    bool reset_window = false; // /loot reset: back to 80,80 760x380
};
LootCommandResult loot_command(PersonalLoot& loot, const std::string& args, std::int64_t now_ms);

} // namespace nms
