#include "nms_petcmd.h"

#include <algorithm>
#include <array>

namespace nms {

namespace {

// The server's simple_commands and toggle_commands (one word each; its aliases are not needed), in its usage order.
constexpr std::array kVerbs{
    PetVerb{"attack", "Attack", false, true},     PetVerb{"qattack", "Queue Attack", false, true},
    PetVerb{"follow", "Follow", false, false},    PetVerb{"guard", "Guard", false, false},
    PetVerb{"sit", "Sit", false, false},          PetVerb{"stop", "Stop", false, true},
    PetVerb{"taunt", "Taunt", true, false},       PetVerb{"hold", "Hold", true, true},
    PetVerb{"ghold", "Greater Hold", true, true}, PetVerb{"spellhold", "Spell Hold", true, false},
    PetVerb{"focus", "Focus", true, true},        PetVerb{"back", "Back Off", false, true},
    PetVerb{"regroup", "Regroup", true, false},   PetVerb{"assist", "Assist", true, true},
    PetVerb{"health", "Health", false, false},    PetVerb{"leader", "Leader", false, true},
    PetVerb{"feign", "Feign Death", false, false}, PetVerb{"leave", "Dismiss", false, true},
};

// Class ids 1..16 (EQ's), the server's first word for each.
constexpr std::array<std::string_view, 16> kClassWords{"war", "clr", "pal", "rng", "shd", "dru", "mnk", "brd",
                                                       "rog", "shm", "nec", "wiz", "mag", "enc", "bst", "ber"};

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

} // namespace

std::span<const PetVerb> pet_verbs() { return kVerbs; }

const PetVerb* find_pet_verb(std::string_view word) {
    for (const PetVerb& v : kVerbs)
        if (word == v.word) return &v;
    return nullptr;
}

int pet_verb_button(std::string_view word) {
    // zone/common.h PET_BUTTON_*, set by the setters #petcmd's DoPetCommand reaches (Mob::SetHeld, SetGHeld,
    // SetFocused, SetNoCast, SetPetRegroup; NPC::DoPetCommandTaunt) when the pet is the owner's focused_pet_id.
    if (word == "regroup") return 2;
    if (word == "taunt") return 5;
    if (word == "hold") return 6;
    if (word == "ghold") return 7;
    if (word == "focus") return 8;
    if (word == "spellhold") return 9;
    return -1;
}

const char* pet_class_name(std::uint32_t id) {
    static constexpr const char* kNames[] = {"Warrior", "Cleric", "Paladin", "Ranger", "Shadow Knight", "Druid", "Monk",
                                             "Bard", "Rogue", "Shaman", "Necromancer", "Wizard", "Magician",
                                             "Enchanter", "Beastlord", "Berserker"};
    return id >= 1 && id <= 16 ? kNames[id - 1] : "Unknown";
}

std::string_view pet_class_word(std::uint32_t class_id) {
    return class_id >= 1 && class_id <= kClassWords.size() ? kClassWords[class_id - 1] : std::string_view();
}

std::optional<std::string> petcmd_line(std::string_view verb, std::string_view state, std::string_view target) {
    const PetVerb* v = find_pet_verb(verb);
    if (!v) return std::nullopt;
    if (!state.empty() && (!v->toggle || (state != "on" && state != "off"))) return std::nullopt;
    const bool swarm = target == "swarm";
    if (!swarm && target != "all" && std::find(kClassWords.begin(), kClassWords.end(), target) == kClassWords.end())
        return std::nullopt;
    if (swarm && !v->swarm) return std::nullopt;
    std::string line = "#petcmd ";
    line += v->word;
    if (!state.empty()) (line += ' ') += state;
    (line += ' ') += target;
    return line;
}

std::optional<std::pair<std::string, bool>> swarm_setting(std::string_view line) {
    // handleSwarmToggle: "Swarm pet {} setting: {}" with ON or OFF.
    constexpr std::string_view head = "Swarm pet ", mid = " setting: ";
    if (!starts_with(line, head)) return std::nullopt;
    line.remove_prefix(head.size());
    const auto at = line.find(mid);
    if (at == std::string_view::npos || at == 0) return std::nullopt;
    const std::string_view value = line.substr(at + mid.size());
    if (value != "ON" && value != "OFF") return std::nullopt;
    return std::pair{std::string(line.substr(0, at)), value == "ON"};
}

std::optional<std::string> PetCommands::command(std::string_view verb, std::string_view state,
                                                std::string_view target) {
    auto line = petcmd_line(verb, state, target);
    if (line) echoes_.push_back("You say, '" + *line + "'");
    return line;
}

std::optional<std::string> PetCommands::query_swarm() {
    if (queried_) return std::nullopt;
    queried_ = true;
    query_answers_ = 4;
    std::string line(kSwarmQuery);
    echoes_.push_back("You say, '" + line + "'");
    return line;
}

bool PetCommands::on_chat(std::string_view text) {
    if (auto it = std::find(echoes_.begin(), echoes_.end(), text); it != echoes_.end()) {
        echoes_.erase(echoes_.begin(), it + 1); // an echo that never came is not waited for
        return true;
    }
    if (auto s = swarm_setting(text)) {
        swarm_[s->first] = s->second;
        if (query_answers_ > 0) {
            --query_answers_;
            return true;
        }
    }
    return false;
}

void PetCommands::on_button_state(std::uint32_t button, bool on) {
    if (button < buttons_.size()) buttons_[button] = on;
}

std::optional<bool> PetCommands::focused(std::string_view verb) const {
    const int b = pet_verb_button(verb);
    return b < 0 ? std::nullopt : buttons_[static_cast<std::size_t>(b)];
}

void PetCommands::zoned() {
    echoes_.clear();
    query_answers_ = 0;
    queried_ = false; // the settings are the server's: asked again in the next zone
}

} // namespace nms
