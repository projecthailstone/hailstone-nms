// #petcmd, the NMS server's command for several pets at once (its zone/gm_commands/petcmd.cpp): command words, then
// the pets they are for ("all", the default; "swarm"; or the class that summoned them, as OP_PetList gives it). The
// NMS Pet window's buttons send it as "/say #petcmd <command> <target>". Hailstone's, not the NMS patch's: the patch
// has no window for it.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nms {

struct PetVerb {
    const char* word;  // the server's word
    const char* label; // the button's
    bool toggle;       // takes "on" / "off" (without either the server flips it; the window always says which)
    bool swarm;        // swarm pets take it (the server's swarm_unsupported_commands)
};
// In the server's usage order.
std::span<const PetVerb> pet_verbs();
const PetVerb* find_pet_verb(std::string_view word);

// multi_pet.cpp GetClassName: the summoning class OP_PetList carries ("Unknown" outside 1..16).
const char* pet_class_name(std::uint32_t class_id);
// The server's word for a class (its class_mappings: war .. ber), "" for 0 (a charmed pet) and anything else.
std::string_view pet_class_word(std::uint32_t class_id);

// "#petcmd <verb> [on|off] <target>", or nothing for a word, state or target the server does not take, a state on a
// command that has none, or a command swarm pets do not take sent to them.
std::optional<std::string> petcmd_line(std::string_view verb, std::string_view state, std::string_view target);

// The swarm pets' four saved switches (hold, ghold, focus, assist): a switch word without on/off makes the server
// report it ("Swarm pet hold setting: ON"), so one line asks all four.
inline constexpr std::string_view kSwarmQuery = "#petcmd hold ghold focus assist swarm";
// "Swarm pet <name> setting: ON|OFF" -> {name, on}.
std::optional<std::pair<std::string, bool>> swarm_setting(std::string_view line);

// What the window sent, for its chat: the echoes of its own lines ("You say, '#petcmd ...'") and the answers to its
// swarm query are hidden; any swarm setting line, the player's own too, updates the settings.
class PetCommands {
public:
    // The line to send (and remembers its echo), or nothing (petcmd_line).
    std::optional<std::string> command(std::string_view verb, std::string_view state, std::string_view target);
    // The swarm query's line, once until the next zone: the window asks when it first shows.
    std::optional<std::string> query_swarm();
    // OnFilterChat: true hides the line.
    bool on_chat(std::string_view text);
    void zoned();

    // hold, ghold, focus, assist: as the server last said; absent until it said.
    const std::map<std::string, bool>& swarm() const { return swarm_; }

private:
    std::deque<std::string> echoes_; // "You say, '...'" lines still to come, oldest first
    std::map<std::string, bool> swarm_;
    int query_answers_ = 0; // the query's lines still to come
    bool queried_ = false;
};

} // namespace nms
