#include "nms_rules.h"

#include <algorithm>
#include <format>

namespace nms {

namespace {

constexpr const char* kCodes[16] = {"WAR", "CLR", "PAL", "RNG", "SHD", "DRU", "MNK", "BRD",
                                    "ROG", "SHM", "NEC", "WIZ", "MAG", "ENC", "BST", "BER"};

// who_multiclass.cpp GetRaceName: the playable races only.
const char* who_race(std::uint32_t race) {
    switch (race) {
    case 1: return "Human";      case 2: return "Barbarian"; case 3: return "Erudite";   case 4: return "Wood Elf";
    case 5: return "High Elf";   case 6: return "Dark Elf";  case 7: return "Half Elf";  case 8: return "Dwarf";
    case 9: return "Troll";      case 10: return "Ogre";     case 11: return "Halfling"; case 12: return "Gnome";
    case 128: return "Iksar";    case 130: return "Vah Shir"; case 330: return "Froglok"; case 522: return "Drakkin";
    default: return "Unknown";
    }
}

// who_multiclass.cpp GetRankTag: the rank eqstr ids the server sends.
const char* who_rank(std::uint32_t id) {
    switch (id) {
    case 5007: return " * GM-Admin * ";      case 5008: return " * ApprenticeGuide * "; case 5009: return " * Guide * ";
    case 5010: return " * ElderGuide * ";    case 5011: return " * QuestMaster * ";     case 5012: return " * GM * ";
    case 5013: return " * GM-Areas * ";      case 5014: return " * GM-Coder * ";
    case 5015: return " * GM-Customer Service * "; case 5016: return " * GM-Event * ";
    case 5017: return " * GM-Lead Admin * "; case 5018: return " * GM-Lead Guide * ";   case 5019: return " * GM-Loot * ";
    case 5020: return " * GMMgmt * ";        case 5021: return " * GM-Impossible * ";
    case 12315: return " TRADER ";           case 6056: return " BUYER ";
    default: return "";
    }
}

std::string mask_codes(std::uint32_t mask) {
    std::string out;
    for (std::uint32_t c = 1; c <= 16; ++c)
        if (has_class(mask, c)) out += (out.empty() ? "" : "/") + std::string(kCodes[c - 1]);
    return out.empty() ? "Unknown" : out;
}

} // namespace

std::uint8_t mask_min_level(const LevelOf& orig, std::uint32_t mask) {
    std::uint8_t best = 255;
    for (std::uint32_t c = 1; c <= 16; ++c)
        if (has_class(mask, c)) best = std::min(best, orig(c));
    return best;
}

std::uint8_t spell_level_needed(const LevelOf& orig, std::uint32_t arg, std::uint32_t visible, std::uint32_t mask,
                                bool player, bool scribe) {
    if (!scribe) {
        if (mask == 0 || !player) return orig(arg);
        if (arg == visible || has_class(mask, arg)) return mask_min_level(orig, mask);
        return 255;
    }
    std::uint8_t r = 255;
    if (mask && player && arg == visible) r = mask_min_level(orig, mask) < 255 ? 1 : 255;
    if (r == 255) {
        r = orig(arg);
        if (r != 255) r = 1;
    }
    return r;
}

std::uint32_t usable_classes(std::uint32_t item_classes, std::uint32_t mask, std::uint32_t visible) {
    if (item_classes == 0 || (item_classes & mask)) return 0xffff;
    std::uint32_t v = item_classes & ~class_bit(visible);
    return v ? v : 1;
}

std::uint32_t cast_spoof_class(const LevelOf& orig, std::uint32_t mask) {
    for (std::uint32_t c = 1; c <= 16; ++c) {
        if (!has_class(mask, c)) continue;
        std::uint8_t l = orig(c);
        if (l >= 1 && l <= 254) return c;
    }
    return 0;
}

std::optional<bool> skill_gate(std::int32_t skill, std::uint32_t mask, std::int32_t profile_skill) {
    if (skill >= 0 && skill < 100 && profile_skill == -1) return false;
    if (skill == kSkillTracking && mask != 0 && !(mask & kTrackingMask)) return false;
    return std::nullopt;
}

bool tracking_allowed(std::uint8_t, std::uint32_t class_mask, std::uint32_t tracking_skill) {
    if (class_mask != 0 && !(class_mask & kTrackingMask)) return false;
    return tracking_skill > 0;
}

std::uint32_t skill_spoof_class(std::int32_t skill, std::uint32_t mask) {
    auto first = [mask](std::initializer_list<std::uint32_t> classes) -> std::uint32_t {
        for (std::uint32_t c : classes)
            if (has_class(mask, c)) return c;
        return 0;
    };
    switch (skill) {
    case 8: case 17: case 29: case 39: case 42: case 48: case 71: return first({9});   // Rogue
    case 30: return first({1, 7, 4, 15, 16});                                        // Kick
    case 10: return first({1, 3, 5, 2});                                             // Bash
    case 74: return first({16});                                                     // Frenzy
    case 73: return first({1, 3, 5, 4});                                             // Taunt
    case 21: case 23: case 26: case 38: case 52: return first({7});                  // monk specials
    default: return 0;
    }
}

std::string class_code(std::uint32_t class_id) {
    return class_id >= 1 && class_id <= 16 ? kCodes[class_id - 1] : "UNK";
}

std::string level_needed_line(const LevelOf& orig, std::uint32_t mask, std::uint32_t visible) {
    auto codes = [&orig](auto&& wanted) {
        std::string out;
        for (std::uint32_t c = 1; c <= 16; ++c) {
            if (!wanted(c)) continue;
            std::uint8_t l = orig(c);
            if (l == 0 || l == 255) continue;
            if (!out.empty()) out += ' ';
            out += std::format("{}({})", kCodes[c - 1], l);
        }
        return out;
    };
    std::string mine = mask ? codes([mask](std::uint32_t c) { return has_class(mask, c); })
                            : codes([visible](std::uint32_t c) { return c == visible; });
    if (!mine.empty()) return "Level Needed: " + mine;
    // Hailstone difference: the patch stops at "CANNOT USE"; the classes that can use the scroll follow it here.
    std::string all = codes([](std::uint32_t) { return true; });
    return all.empty() ? "Level Needed: CANNOT USE" : "Level Needed: CANNOT USE - " + all;
}

std::optional<std::uint8_t> list_level(const LevelOf& orig, std::uint32_t mask) {
    std::uint8_t m = mask_min_level(orig, mask);
    if (m >= 1 && m < 255) return m;
    std::uint8_t all = mask_min_level(orig, 0xffff);
    if (all >= 1 && all < 255) return all;
    return std::nullopt;
}

std::vector<std::string> format_who(const std::vector<WhoEntry>& entries, const ZoneNameFn& zone_name) {
    std::vector<std::string> lines;
    for (const auto& e : entries) {
        std::uint32_t v = e.class_value;
        std::string klass;
        if (v & (v - 1)) klass = mask_codes(v);               // more than one bit: a mask
        else if (v >= 1 && v <= 16) klass = kCodes[v - 1];    // a class id
        else if (v != 0) klass = mask_codes(v);               // one class as a mask
        else klass = "Unknown";
        std::string zone;
        if (e.zone_string == 5006) {
            std::string z = zone_name ? zone_name(e.zone_id) : std::string();
            if (!z.empty()) zone = " ZONE: " + z;
        }
        const char* rank = who_rank(e.rank_string);
        if (e.format == 5024)
            lines.push_back(std::format("[{}] - [ANONYMOUS]{}{}", e.name, zone, rank));
        else if (e.format == 5023)
            lines.push_back(std::format("[{}] - [{} {} ({})] <ROLEPLAY>{}{}", e.name, e.level, klass, who_race(e.race), zone, rank));
        else
            lines.push_back(std::format("[{}] - [{} {} ({})]{}{}", e.name, e.level, klass, who_race(e.race), zone, rank));
    }
    const std::size_t n = entries.size();
    if (n == 0) lines.emplace_back("There are no players in EverQuest that match those who filters.");
    else if (n == 1) lines.emplace_back("There is 1 player in EverQuest.");
    else lines.push_back(std::format("There are {} players in EverQuest.", n));
    return lines;
}

std::uint32_t DisciplineTimers::get(std::int32_t id) const {
    for (const auto& [k, v] : timers_)
        if (k == id) return v;
    return 0;
}

void DisciplineTimers::set(std::int32_t id, std::uint32_t ready) {
    for (auto& [k, v] : timers_)
        if (k == id) { v = ready; return; }
    timers_.emplace_back(id, ready);
}

Vitals server_vitals(const std::map<std::uint32_t, std::int64_t>& stats) {
    auto stat = [&](std::uint32_t key) -> std::optional<std::int32_t> {
        auto it = stats.find(key);
        if (it == stats.end()) return std::nullopt;
        return it->second >= 0x7ffffffe ? 0x7ffffffe : static_cast<std::int32_t>(static_cast<std::uint32_t>(it->second));
    };
    Vitals v;
    v.cur_hp = stat(2);
    v.cur_mana = stat(3);
    v.cur_endurance = stat(4);
    v.max_hp = stat(5);
    v.max_mana = stat(6);
    v.max_endurance = stat(7);
    return v;
}

std::optional<std::int32_t> discipline_band(std::uint32_t spell_id) {
    struct Band {
        std::uint32_t spell;
        std::int32_t timer;
    };
    static constexpr Band kBands[] = {
#include "nms_discipline_bands.inc"
    };
    std::optional<std::int32_t> out; // the loop writes each entry in turn: the last one for a spell wins
    for (const Band& b : kBands)
        if (b.spell == spell_id) out = b.timer;
    return out;
}

std::string discipline_not_ready_line(std::string_view name, std::uint32_t seconds) {
    return std::format("{} will be ready in {} seconds.", name, static_cast<std::int32_t>(seconds));
}

// FUN_100ecf40.
void Cooldowns::set(std::size_t slot, float seconds, std::uint32_t now_ms) {
    if (slot >= seconds_.size()) return;
    if (seconds > 0.0f) {
        seconds_[slot] = seconds;
        set_at_[slot] = now_ms;
        if (longest_[slot] < seconds) longest_[slot] = seconds;
    } else {
        seconds_[slot] = 0.0f;
    }
}

// The 0x1339 case of the HandleWorldMessage detour (u32 values converted as unsigned).
void Cooldowns::on_skill_timers(const SkillTimers& t, std::uint32_t now_ms) {
    autoskill_ = t.autoskill;
    const float kick = static_cast<float>(t.kick), strike = static_cast<float>(t.strike);
    set(0, static_cast<float>(t.backstab), now_ms);
    set(1, static_cast<float>(t.bash), now_ms);
    set(2, kick, now_ms);
    set(3, static_cast<float>(t.frenzy), now_ms);
    set(7, kick, now_ms);
    set(8, kick, now_ms);
    set(5, strike, now_ms);
    set(6, strike, now_ms);
    set(9, strike, now_ms);
}

// FUN_1000a960's countdown: (GetTickCount - set) as unsigned / 1000, subtracted, clamped at 0.
float Cooldowns::remaining(std::size_t slot, std::uint32_t now_ms) const {
    if (slot >= seconds_.size() || !(seconds_[slot] > 0.0f)) return 0.0f;
    const float elapsed = static_cast<float>(static_cast<double>(now_ms - set_at_[slot])) / 1000.0f;
    const float left = seconds_[slot] - elapsed;
    return left < 0.0f ? 0.0f : left;
}

std::uint8_t scribe_class(std::uint32_t item_classes, std::uint32_t mask) {
    const std::uint32_t both = item_classes & mask;
    const std::uint32_t active = both != 0 ? both : item_classes;
    for (std::uint32_t i = 0; i < 16; ++i)
        if (active & (1u << i)) return static_cast<std::uint8_t>(i + 1);
    return 0;
}

bool scribe_is_tome(std::uint8_t item_type, const std::string& name) {
    if (item_type == 31) return true;
    if (item_type != 20) return false;
    for (const char* w : {"Tome", "tome", "Skill", "skill", "Discipline", "discipline"})
        if (name.find(w) != std::string::npos) return true;
    return false;
}

bool hide_patch_line(std::string_view text) {
    auto has = [text](std::string_view s) { return text.find(s) != std::string_view::npos; };
    if (has("VAULTDATA|") || has("--- DIMENSIONAL POCKET VAULT ---") || has("Pages: ") ||
        has("Put an item or bag on your cursor and click 'Deposit Cursor'") ||
        (has("Slot ") && (has("Withdraw") || has("Deposit Cursor") || has("<Empty>"))) ||
        has("_______________________________________________"))
        return true;
    if (has("You say, '") && (has("vault_page") || has("vault_deposit") || has("vault_withdraw") ||
                               has("vault_bank") || has("vault_merchant") || has("#autoskill")))
        return true;
    return has("launching the welcome screen");
}

bool xtarget_server_pet_slot(std::uint32_t slot_type, std::uint32_t spawn_id, std::uint32_t spawn_owner_id,
                             std::uint32_t player_id, const std::vector<std::uint32_t>& pet_ids) {
    if (slot_type > 1 || spawn_id == 0 || spawn_id == player_id) return false;
    if (player_id != 0 && spawn_owner_id == player_id) return true;
    return std::find(pet_ids.begin(), pet_ids.end(), spawn_id) != pet_ids.end();
}

} // namespace nms
