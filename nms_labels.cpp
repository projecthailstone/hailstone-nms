// The NMS plugin's values for the client's windows: the patch's labels, the item display's "Level Needed" line and
// the merchant's Lvl column (NMS/dinput8.dll; rof2decomp notes/nms-dinput8.md).

#include <cstdio>

#include "nms_plugin.h"

namespace nms {

namespace {

// ---- The NMS client patch's labels (dinput8.dll; NMS servers with Custom:ServerAuthStats) ----
// The patch hooks __GetLabelFromEQ (0x10156540): it runs the original (the client's label), then for the types in
// its table replaces the text with one built from the server's OP_ServerAuthStats values (Nms::stats). The colour
// stays the original's. Decoded from the handler table it builds at
// 0x10153d30 (type -> handler) and 0x101541c0 (type -> StatEntry key); docs/unknowns.md.

// The class titles by level (0x10154d10: the player's level, >= 75 / 70 / 65 / 60 / 55 / 51), as the patch spells
// them (no spaces).
std::string class_title(int cls, int level) {
    static constexpr const char* kTitles[16][7] = {
        {"Imperator", "Vanquisher", "Overlord", "Warlord", "Myrmidon", "Champion", "Warrior"},
        {"Exemplar", "Prelate", "Archon", "HighPriest", "Templar", "Vicar", "Cleric"},
        {"HolyDefender", "Lord", "LordProtector", "Crusader", "Knight", "Cavalier", "Paladin"},
        {"Huntmaster", "Plainswalker", "ForestStalker", "Warder", "Outrider", "Pathfinder", "Ranger"},
        {"Bloodreaver", "ScourgeKnight", "DreadLord", "GraveLord", "Revenant", "Reaver", "ShadowKnight"},
        {"StormCaller", "Natureguard", "StormWarden", "Hierophant", "Preserver", "Wanderer", "Druid"},
        {"Ashenhand", "StoneFist", "Transcendent", "Grandmaster", "Master", "Disciple", "Monk"},
        {"Lyricist", "Performer", "Maestro", "Virtuoso", "Troubadour", "Minstrel", "Bard"},
        {"Shadowblade", "Nemesis", "Deceiver", "Assassin", "Blackguard", "Rake", "Rogue"},
        {"Spiritwatcher", "Soothsayer", "Prophet", "Oracle", "Luminary", "Mystic", "Shaman"},
        {"Deathcaller", "Wraith", "ArchLich", "Warlock", "Defiler", "Heretic", "Necromancer"},
        {"Pyromancer", "GrandArcanist", "Arcanist", "Sorcerer", "Evoker", "Channeler", "Wizard"},
        {"GrandSummoner", "ArchMagus", "ArchConvoker", "ArchMage", "Conjurer", "Elementalist", "Magician"},
        {"Entrancer", "Bedazzler", "Coercer", "Phantasmist", "Beguiler", "Illusionist", "Enchanter"},
        {"Frostblood", "Wildblood", "FeralLord", "SavageLord", "Animist", "Primalist", "Beastlord"},
        {"Juggernaut", "Ravager", "Fury", "Rager", "Vehement", "Brawler", "Berserker"}};
    static constexpr int kLevels[6] = {75, 70, 65, 60, 55, 51};
    if (cls < 1 || cls > 16) return {};
    for (int i = 0; i < 6; ++i)
        if (level >= kLevels[i]) return kTitles[cls - 1][i];
    return kTitles[cls - 1][6];
}

// The table at 0x103429b8.
constexpr const char* kClassAbbr[17] = {"", "WAR", "CLR", "PAL", "RNG", "SHD", "DRU", "MNK", "BRD",
                                           "ROG", "SHM", "NEC", "WIZ", "MAG", "ENC", "BST", "BER"};

using Stats = std::map<std::uint32_t, std::int64_t>;

const std::int64_t* server_stat(const Stats& m, std::uint32_t key) {
    auto it = m.find(key);
    return it == m.end() ? nullptr : &it->second;
}

// 0x10155530: the value as a decimal number, or " " without one.
std::string stat_value(const Stats& c, std::uint32_t key) {
    const std::int64_t* v = server_stat(c, key);
    return v ? std::to_string(*v) : std::string(" ");
}

// 0x10155a40 / bc0 / d40: cur * 100 / max (doubles, truncated); " " without both keys, max <= 0 or cur < 0.
std::string stat_percent(const Stats& c, std::uint32_t cur_key, std::uint32_t max_key) {
    const std::int64_t* cur = server_stat(c, cur_key);
    const std::int64_t* max = server_stat(c, max_key);
    if (!cur || !max || *max <= 0 || *cur < 0) return " ";
    return std::to_string(static_cast<int>(static_cast<double>(*cur) / static_cast<double>(*max) * 100.0));
}

// The classes in the bit mask (key 1), one per line (0x101555d0: the titles, "None" without classes; 0x10155830:
// the abbreviations, "" without).
std::string classes(const Stats& c, bool titles) {
    const char* none = titles ? "None" : "";
    const std::int64_t* mask = server_stat(c, 1);
    if (!mask || *mask == 0) return none;
    const NmscPlayer me = player();
    int level = static_cast<int>(me.level);
    if (titles && !player_spawned(me)) return none;
    std::string out;
    for (int cls = 1; cls <= 16; ++cls) {
        if (!(*mask & (std::int64_t{1} << (cls - 1)))) continue;
        if (!out.empty()) out += '\n';
        out += titles ? class_title(cls, level) : kClassAbbr[cls];
    }
    return out.empty() ? std::string(none) : out;
}

// The type -> key table (0x10379564) for the types the generic handler (0x101557a0) answers.
std::optional<std::uint32_t> stat_key(int type) {
    switch (type) {
    case 17: return 2;   case 124: return 3;  case 126: return 4;  case 18: return 5;   case 125: return 6;
    case 127: return 7;  case 5: return 0xa;  case 6: return 0xb;  case 7: return 0xc;  case 8: return 0xd;
    case 9: return 0xf;  case 10: return 0xe; case 11: return 0x10; case 16: return 0x11; case 15: return 0x13;
    case 14: return 0x12; case 12: return 0x15; case 13: return 0x14; case 23: return 0x19; case 22: return 0x27;
    case 1018: return 8; case 221: return 0x30; case 1017: return 1; case 212: return 0x16; case 213: return 0x17;
    case 214: return 0x18; case 227: return 0x34; case 1009: case 1010: case 1011: return 0x1a;
    case 1012: return 0x46; case 1013: return 0x4c; case 1014: return 0x48; case 1003: return 0x28;
    case 1002: return 0x44; case 1004: return 0x29;
    default: return std::nullopt;
    }
}

// The patch's handler for `type`: its text, or nullopt when the type is not in its table.
std::optional<std::string> label_handler(const Stats& c, int type) {
    switch (type) {
    case 3: return classes(c, true);
    case 6666: return classes(c, false);
    case 6668: return stat_value(c, 8);    // through 1018: mitigation
    case 6667: return stat_value(c, 0x30); // through 221: avoidance
    case 6671: return stat_value(c, 0x46); case 6672: return stat_value(c, 0x47); case 6673: return stat_value(c, 0x48);
    case 6674: return stat_value(c, 0x49); case 6675: return stat_value(c, 0x4a); case 6676: return stat_value(c, 0x4b);
    case 6677: return stat_value(c, 0x4c); case 6678: return stat_value(c, 0x4d); case 6707: return stat_value(c, 0x74);
    case 6669: return stat_value(c, 0x44); case 6703: return stat_value(c, 0x73); case 6704: return stat_value(c, 0x70);
    case 6705: return stat_value(c, 0x71); case 6706: return stat_value(c, 0x72);
    case 70: return stat_value(c, 2) + " / " + stat_value(c, 5);
    case 128: return stat_value(c, 3) + " / " + stat_value(c, 6);
    case 129: return stat_value(c, 4) + " / " + stat_value(c, 7);
    case 20: return stat_percent(c, 3, 6);
    case 21: return stat_percent(c, 4, 7);
    case 542: case 1019: return " "; // in the handler table, not in the key table
    default:
        if (type >= 1020) return std::nullopt;
        if (auto key = stat_key(type)) return stat_value(c, *key);
        return std::nullopt;
    }
}

} // namespace

std::optional<std::string> Nms::label(int type, const std::string& text) const {
    const Stats& c = stats;
    if (c.empty()) return std::nullopt;
    if (type == 19) {
        // Only while the target is the player (pinstTarget's spawn id == pinstLocalPlayer's).
        const NmscPlayer me = player();
        if (me.target_id != 0 && me.target_id == me.spawn_id) return stat_percent(c, 2, 5);
        return std::nullopt;
    }
    if (type >= 5 && type <= 11) {
        // The stats: the server's value replaces the text before the first '/' (there is none in the stock text).
        auto slash = text.find('/');
        auto key = stat_key(type);
        const std::int64_t* val = key ? server_stat(c, *key) : nullptr;
        if (slash == std::string::npos || !val) return std::nullopt;
        return std::to_string(static_cast<std::int32_t>(*val)) + text.substr(slash);
    }
    if (type == 338) {
        // EOM points: PlayerPointManager::GetAltCurrency(6, 1) (0x10156f90); alternate currencies are not kept by
        // this client, so the label stays empty rather than showing the original's "Unknown Label338".
        return std::string();
    }
    auto t = label_handler(c, type);
    if (t && !t->empty()) return t;
    return std::nullopt;
}

std::optional<std::string> Nms::item_level_line(std::uint32_t spell_id) const {
    // The patch's "Level Needed: CLR(1) ..." line (FUN_101507b0 / ScrubTooltipLevelNeeded), with a class mask;
    // without one the stock line.
    const std::uint32_t mask = class_mask();
    if (mask == 0) return std::nullopt;
    const auto s = spell(spell_id);
    if (!s) return std::nullopt;
    auto orig = [&s](std::uint32_t k) { return s->class_level[k - 1]; };
    return nms::level_needed_line(orig, mask, player().visible_class);
}

std::optional<std::string> Nms::list_level(const NmscItem& item) const {
    // The NMS patch's merchant Lvl column (CMerchantWnd::UpdateList detour, notes/nms-dinput8.md §3.6): for a spell
    // scroll the lowest level over the class mask ("%3d" so the string sort orders it), else over all 16 classes
    // when one can use it, else "--"; nullopt for anything else or without a class mask.
    const std::uint32_t mask = class_mask();
    if (!mask || item.item_type != 20 || item.scroll_spell == 0) return std::nullopt;
    const auto s = spell(item.scroll_spell);
    if (!s) return std::nullopt;
    auto lvl = nms::list_level([&s](std::uint32_t k) { return s->class_level[k - 1]; }, mask);
    if (!lvl) return std::string("--");
    char b[8];
    std::snprintf(b, sizeof b, "%3d", *lvl);
    return std::string(b);
}

} // namespace nms
