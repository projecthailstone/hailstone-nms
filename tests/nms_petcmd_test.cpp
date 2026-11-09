// The Pet window's #petcmd (plugins/nms/nms_petcmd.h): the lines it sends and the switch states it
// lights its On / Off buttons by (the swarm settings in chat, the focused pet's OP_PetCommandState).
#include <doctest/doctest.h>

#include <cstddef>
#include <string_view>
#include <vector>

#include "nms_packets.h"
#include "nms_petcmd.h"

using namespace nms;

TEST_CASE("Nms #petcmd: the lines the window sends") {
    CHECK(petcmd_line("hold", "on", "all") == "#petcmd hold on all");
    CHECK(petcmd_line("taunt", "off", "mag") == "#petcmd taunt off mag");
    CHECK(petcmd_line("attack", "", "swarm") == "#petcmd attack swarm");
    CHECK_FALSE(petcmd_line("attack", "on", "all"));  // not a switch
    CHECK_FALSE(petcmd_line("taunt", "", "swarm"));   // swarm pets do not take it
    CHECK_FALSE(petcmd_line("hold", "on", "nobody")); // not a target
}

TEST_CASE("Nms #petcmd: the switches the server's PET_BUTTON_* set") {
    CHECK(pet_verb_button("regroup") == 2);
    CHECK(pet_verb_button("taunt") == 5);
    CHECK(pet_verb_button("hold") == 6);
    CHECK(pet_verb_button("ghold") == 7);
    CHECK(pet_verb_button("focus") == 8);
    CHECK(pet_verb_button("spellhold") == 9);
    CHECK(pet_verb_button("assist") == -1); // the server sends no state for it
    CHECK(pet_verb_button("attack") == -1);
    // Every toggle the window draws but assist has a button.
    for (const PetVerb& v : pet_verbs())
        if (v.toggle) CHECK((pet_verb_button(v.word) >= 0) == (std::string_view(v.word) != "assist"));
}

TEST_CASE("Nms #petcmd: OP_PetCommandState sets the focused pet's switches") {
    const Packet p = encode_pet_command_state(6, 1);
    CHECK(p.opcode == op::PetCommandState);
    const auto st = decode_pet_command_state(p.data);
    REQUIRE(st);
    CHECK(st->first == 6);
    CHECK(st->second == 1);
    CHECK_FALSE(decode_pet_command_state(std::vector<std::byte>(7)));

    PetCommands pc;
    CHECK_FALSE(pc.focused("hold")); // unknown until the server says
    pc.on_button_state(6, true);
    pc.on_button_state(5, false);
    pc.on_button_state(10, true); // outside PET_BUTTON_*: ignored
    CHECK(pc.focused("hold") == true);
    CHECK(pc.focused("taunt") == false);
    CHECK_FALSE(pc.focused("ghold"));
    CHECK_FALSE(pc.focused("assist"));
    // The server turns hold off when greater hold goes on (Mob::SetGHeld): both come.
    pc.on_button_state(7, true);
    pc.on_button_state(6, false);
    CHECK(pc.focused("ghold") == true);
    CHECK(pc.focused("hold") == false);
    // OnZoned comes after the new zone sent them: kept. A new zone connection forgets them.
    pc.zoned();
    CHECK(pc.focused("ghold") == true);
    pc.zone_changed();
    CHECK_FALSE(pc.focused("ghold"));
}
