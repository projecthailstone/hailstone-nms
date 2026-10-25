#include "plugins/nms/nms_rules.h"

#include <algorithm>
#include <format>

#include "game/zone_names.h"
#include "net/social_packets.h"

namespace nmsc::nms {

namespace {

constexpr const char* kAbbrevs[16] = {"War", "Clr", "Pal", "Rng", "Shd", "Dru", "Mnk", "Brd",
                                      "Rog", "Shm", "Nec", "Wiz", "Mag", "Enc", "Bst", "Ber"};
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

std::string class_abbrevs(std::uint32_t mask) {
    std::string out;
    for (std::uint32_t c = 1; c <= 16; ++c)
        if (has_class(mask, c)) out += (out.empty() ? "" : "/") + std::string(kAbbrevs[c - 1]);
    return out;
}

std::string class_code(std::uint32_t class_id) {
    return class_id >= 1 && class_id <= 16 ? kCodes[class_id - 1] : "UNK";
}

std::string class_desc(std::int32_t id, const std::function<std::string(std::int32_t)>& desc,
                       const std::function<std::uint32_t(std::int32_t)>& slot_mask,
                       const std::function<std::uint32_t(std::int32_t)>& slot_class) {
    if (id >= 1000) return class_abbrevs(static_cast<std::uint32_t>(id - 1000));
    if (id >= 200 && id <= 449) {
        const std::int32_t slot = id - 200;
        if (std::uint32_t m = slot_mask(slot)) {
            std::string s = class_abbrevs(m);
            if (!s.empty()) return s;
        }
        std::uint32_t c = slot_class(slot);
        return desc(c ? static_cast<std::int32_t>(c) : 1);
    }
    if (id < 1 || id > 450) return desc(1);
    return desc(id);
}

std::string window_title(const std::string& name, std::uint32_t mask) {
    std::string t = "EverQuest - " + name;
    if (mask) t += " [" + class_abbrevs(mask) + "]";
    return t;
}

std::string level_needed_line(const LevelOf& orig, std::uint32_t mask, std::uint32_t visible) {
    std::string out = "Level Needed: ";
    bool any = false;
    auto add = [&](std::uint32_t c) {
        std::uint8_t l = orig(c);
        if (l == 0 || l == 255) return;
        if (any) out += ' ';
        out += std::format("{}({})", kCodes[c - 1], l);
        any = true;
    };
    if (mask) {
        for (std::uint32_t c = 1; c <= 16; ++c)
            if (has_class(mask, c)) add(c);
    } else if (visible >= 1 && visible <= 16) {
        add(visible);
    }
    return any ? out : "Level Needed: CANNOT USE";
}

std::optional<std::uint8_t> list_level(const LevelOf& orig, std::uint32_t mask) {
    std::uint8_t m = mask_min_level(orig, mask);
    if (m >= 1 && m < 255) return m;
    std::uint8_t all = mask_min_level(orig, 0xffff);
    if (all >= 1 && all < 255) return all;
    return std::nullopt;
}

std::vector<std::string> format_who(const net::WhoResponse& r) {
    std::vector<std::string> lines;
    for (const auto& e : r.entries) {
        std::uint32_t v = e.class_;
        std::string klass;
        if (v & (v - 1)) klass = mask_codes(v);               // more than one bit: a mask
        else if (v >= 1 && v <= 16) klass = kCodes[v - 1];    // a class id
        else if (v != 0) klass = mask_codes(v);               // one class as a mask
        else klass = "Unknown";
        std::string zone;
        if (e.zone_string == 5006) {
            std::string z = game::zone_short_name(e.zone_id);
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
    const std::size_t n = r.entries.size();
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

std::optional<std::int32_t> discipline_band(std::uint32_t spell_id) {
    struct Band {
        std::uint32_t spell;
        std::int32_t timer;
    };
    static constexpr Band kBands[] = {
#include "plugins/nms/nms_discipline_bands.inc"
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

} // namespace nmsc::nms
