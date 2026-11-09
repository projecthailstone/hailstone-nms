// The NMS plugin (plugins/nms) as a client loads it: the built nms module in a plugins/nms/ folder with its Lua
// windows, loaded through plugin/native_plugins.h into a connection (the test world) and fed the NMS server's packets
// through the plugin hooks. It stays out until it sees an NMS server, then gives the core the class masks, the pets
// and the server's vitals, answers the hooks as the patch does, adds its commands and option, and its windows draw
// in the Modern and Gamepad UIs. Its rules and packets alone: nms_rules_test.cpp.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>

#include "app/lua_eq.h"
#include "game/server_ext_chain.h"
#include "game/spells.h"
#include "game/xtarget.h"
#include "net/social_packets.h"
#include "platform/paths.h"
#include "plugin/native_plugins.h"
#include "plugin/ui_bridge.h"
#include "ui/dev_screens.h"
#include "ui/lua/lua_host.h"
#include "mq_test_world.h"
#include "nms_loot.h"
#include "nms_options.h"
#include "nms_packets.h"
#include "nms_rules.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

struct Host {
    plugin::Hooks hooks;
    std::shared_ptr<game::ExtensionChain> chain_ptr = std::make_shared<game::ExtensionChain>();
    game::ExtensionChain& chain = *chain_ptr;
    // The connection asks the plugins' chain and runs the hooks, as the app's does (net::ConnectionOptions).
    mq_test::World world{"hailstone_nms_plugin_test", options()};
    fs::path plugin_dir = world.dir / "plugins";
    plugin::Policy policy;
    plugin::UiBridge bridge;
    std::vector<std::string> printed, ran;
    std::vector<net::AppPacket> sent;
    float max_zoom = 60.0f;
    std::unique_ptr<plugin::NativePlugins> plugins;

    Host() {
        // As a player installs it (or the server sends it): plugins/nms/ with the binary and its ui/.
        const fs::path dir = plugin_dir / "nms";
        fs::create_directories(dir);
        const fs::path built(HAILSTONE_NMS_PLUGIN);
        fs::copy_file(built, dir / built.filename(), fs::copy_options::overwrite_existing);
        fs::copy(HAILSTONE_NMS_UI_DIR, dir / "ui", fs::copy_options::recursive);
        plugin::Rules r; // a server without a hailstone.cfg: a player's plugin may do everything
        r.dir = world.dir / "servers" / "nms";
        policy.set(r);
        plugin::NativeEnv ne;
        ne.hooks = &hooks;
        ne.policy = &policy;
        ne.print = [this](const std::string& t, std::uint32_t) { printed.push_back(t); };
        ne.run = [this](const std::string& l) {
            ran.push_back(l);
            std::string line = l;
            hooks.command(line);
        };
        ne.send = [this](plugin::Stream, const net::AppPacket& p) { sent.push_back(p); };
        ne.dirs = {plugin_dir};
        ne.cache_dir = world.dir / "cache";
        ne.config_dir = world.dir / "config";
        ne.extensions = chain_ptr.get();
        ne.ui = &bridge;
        ne.connection = [this] { return world.conn.get(); };
        ne.max_camera_distance = [this] { return max_zoom; };
        ne.set_max_camera_distance = [this](float d) { max_zoom = d; };
        plugins = std::make_unique<plugin::NativePlugins>(ne);
    }
    ~Host() { plugins.reset(); }
    net::ConnectionOptions options() {
        net::ConnectionOptions o;
        o.hooks = &hooks;
        o.plugins = chain_ptr;
        return o;
    }
    net::GameConnection& c() { return *world.conn; }
    // A packet from the server, as the connection's update passes it: the plugin hooks first.
    bool zone_in(const nms::Packet& p) { return in(plugin::Stream::Zone, p.opcode, p.data); }
    bool world_in(const nms::Packet& p) { return in(plugin::Stream::World, p.opcode, p.data); }
    bool in(plugin::Stream st, std::uint16_t opcode, std::vector<std::byte> data) {
        net::AppPacket p(opcode, std::move(data));
        if (hooks.packet(st, plugin::Direction::In, p) == plugin::Flow::Drop) return false;
        if (st == plugin::Stream::Zone) c().receive_zone_packet_for_test(p);
        else c().receive_world_packet_for_test(p);
        return true;
    }
    bool command(const std::string& line) {
        std::string l = line;
        return hooks.command(l);
    }
    std::vector<std::string> chat() {
        std::vector<std::string> out;
        while (auto e = c().poll_chat()) out.push_back(e->text);
        return out;
    }
    std::optional<plugin::UiValue> call(const char* fn, std::vector<plugin::UiValue> args = {}) {
        return bridge.call("nms", fn, args);
    }
};

nms::Packet character_sets(const char* name, std::uint32_t classes) {
    nms::CharacterSets cs;
    cs.selected_set = cs.default_set = 7;
    cs.sets.push_back({7, "Default"});
    cs.characters.push_back({55, name, 50, classes, {7}});
    return nms::encode(cs);
}

bool contains(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

} // namespace

TEST_CASE("NMS plugin: out until it sees an NMS server, then the patch's rules, classes, vitals and commands") {
    Host h;
    net::GameConnection& c = h.c();
    REQUIRE(h.plugins->load("nms"));
    CHECK(h.bridge.has_plugin("nms")); // its windows and functions are there from the load

    // A stock server: none of it.
    CHECK_FALSE(h.chain.wide_item_links(c));
    CHECK_FALSE(h.chain.aa_any_level(c));
    CHECK(h.chain.hide_check_class(c, 2) == 2);
    CHECK_FALSE(h.chain.is_spellcaster(c).has_value());
    CHECK_FALSE(h.command("/pets"));
    CHECK(h.call("loot")->kind == plugin::UiValue::Kind::Nil); // nil before an NMS server
    CHECK(h.zone_in({0x6f4b, {std::byte{'h'}, std::byte{0}}})); // OP_Weblink passes on
    CHECK(h.bridge.options().empty());
    // The login checksums go out as built.
    net::AppPacket crc(0x0f13, std::vector<std::byte>(16, std::byte{0x5a}));
    h.hooks.packet(plugin::Stream::World, plugin::Direction::Out, crc);
    CHECK(crc.data[0] == std::byte{0x5a});

    // OP_SendCharacterSets: an NMS world. The masks go into the core; the switches come on.
    CHECK_FALSE(h.world_in(character_sets("Tester", 0x3))); // swallowed: the core does not know it
    CHECK(c.classes_mask("Tester") == 0x3);
    CHECK(c.server_class_mask() == 0x3);
    CHECK(h.chain.wide_item_links(c));
    CHECK(h.chain.aa_any_level(c));
    CHECK(h.chain.aa_train_all(c));
    CHECK(h.chain.hide_check_class(c, 2) == 9);
    CHECK(h.chain.can_use_all_items(c)); // a class mask: only the server checks
    CHECK(h.chain.is_spellcaster(c) == true);
    CHECK(c.guild_member_class(1003) == "War/Clr");
    CHECK(c.guild_member_class(2) == "CLR");
    REQUIRE(h.bridge.options().size() == 1);
    CHECK(h.bridge.options()[0].name == "xtarget_hide_pets");
    REQUIRE(h.bridge.menu().size() == 1); // Personal Loot (no Autoplay Console)
    CHECK(h.bridge.menu()[0].command == "/loot show");

    // The login checksums now carry the patch's VM values (never real ids).
    h.hooks.packet(plugin::Stream::World, plugin::Direction::Out, crc);
    CHECK(crc.data[0] == std::byte{0xff}); // 999999999999 = 0xe8d4a50fff
    CHECK(crc.data[8] == std::byte{0x5a});

    // OP_ServerAuthStats: key 1 the live mask; keys 2..7 the server's HP, mana and endurance (and the points now).
    CHECK_FALSE(h.zone_in(nms::encode_server_auth_stats({{1, 0x3}, {2, 2775}, {5, 3100}, {6, 1950}})));
    CHECK(h.zone_in({0x6f4b, {std::byte{'h'}, std::byte{0}}})); // OP_Weblink: on to the core here too
    CHECK(c.server_class_mask() == 0x3);
    const auto v = h.chain.character_vitals(c);
    CHECK(v.cur_hp == 2775);
    CHECK(v.max_hp == 3100);
    CHECK(v.max_mana == 1950);
    CHECK_FALSE(v.cur_mana);
    CHECK(c.player_status().hp == 2775);
    CHECK(c.player_status().max_hp == 3100);
    CHECK(c.player_status().mana == 300); // not sent: as it was
    // ... and its labels (__GetLabelFromEQ): HP "cur / max", the class titles.
    std::string label = "stock";
    h.chain.label_text(c, 70, label);
    CHECK(label == "2775 / 3100");
    label = "stock";
    h.chain.label_text(c, 6666, label);
    CHECK(label == "WAR\nCLR");

    // GetSpellLevelNeeded: the mask's minimum for the visible class and the mask's classes, 255 for the others.
    const game::SpellData* heal = c.spell_table().get(200);
    REQUIRE(heal);
    auto orig = [heal](std::uint32_t k) { return heal->class_level[(k >= 1 && k <= 16 ? k : 1) - 1]; };
    CHECK(h.chain.spell_level_needed(c, *heal, 2) == nms::spell_level_needed(orig, 2, 2, 0x3, true, false));
    CHECK(h.chain.spell_level_needed(c, *heal, 6) == 255);
    CHECK(h.chain.can_use_gem(c, 11) == true);
    CHECK(h.chain.can_use_gem(c, 12) == false);

    // /who: the patch's own lines.
    net::WhoResponse who;
    net::WhoEntry e;
    e.name = "Morsal";
    e.format = 5022;
    e.class_ = 0x3;
    e.level = 60;
    e.race = 1;
    who.entries.push_back(e);
    auto lines = h.chain.who_lines(c, who);
    REQUIRE(lines);
    CHECK((*lines)[0] == "[Morsal] - [60 WAR/CLR (Human)]");

    // Its commands: not the patch's /fov or /camera (the client's options set the field of view and the zoom limit);
    // /alt toggle becomes the server's #alttoggle.
    h.chat();
    CHECK_FALSE(h.command("/fov 90"));
    CHECK_FALSE(h.command("/camera 90"));
    CHECK(h.chain.handler_command(c, "cmd_alternateadv", "toggle 123"));
    CHECK(contains(h.ran, "/say #alttoggle 123"));
    CHECK_FALSE(h.chain.handler_command(c, "cmd_alternateadv", "buy 123"));
    // /autoskill alone opens the client's Ability Book (the patch's Autoplay Console is not ported); with words it is
    // the server's #autoskill.
    h.ran.clear();
    CHECK(h.command("/autoskill"));
    CHECK(h.ran == std::vector<std::string>{"/abilitybook"});
    h.ran.clear();
    CHECK(h.command("/autoskill Kick enable"));
    CHECK(h.ran == std::vector<std::string>{"#autoskill Kick enable"});
    CHECK_FALSE(h.call("autoplay"));
    CHECK(h.command("/pets"));
    CHECK(contains(h.chat(), "You have no pets."));

    // 0x140f from the server: swallowed.
    CHECK_FALSE(h.zone_in({nms::op::ClientReport, {}}));

    // Unloaded: the stock client again.
    h.plugins->unload("nms");
    CHECK(h.chain.empty());
    CHECK_FALSE(h.bridge.has_plugin("nms"));
    CHECK_FALSE(h.command("/pets"));
}

TEST_CASE("NMS plugin: the zone-in's client check, the 0x140f report and the cast spoof") {
    Host h;
    net::GameConnection& c = h.c();
    REQUIRE(h.plugins->load("nms"));
    CHECK_FALSE(h.zone_in({nms::op::ClientReport, {}})); // the server knows the patch: on
    h.zone_in(nms::encode_server_auth_stats({{1, 0x3}}));
    h.sent.clear();
    // OnZoned: OP_CAuth with the mask and the player's spawn id; the pulse then sends the 0x140f report.
    h.hooks.zone(plugin::ZonePhase::Zoned);
    REQUIRE_FALSE(h.sent.empty());
    CHECK(h.sent[0].opcode == nms::op::CAuth);
    CHECK(h.sent[0].data == nms::encode_cauth(0x3, 1, nms::kPatchAuthKey).data);
    h.hooks.pulse();
    CHECK(std::any_of(h.sent.begin(), h.sent.end(), [](const net::AppPacket& p) { return p.opcode == nms::op::ClientReport; }));
    // The echo: the check accepted, swallowed.
    CHECK_FALSE(h.zone_in({nms::op::CAuth, {}}));
    // No cast running: the global spell recovery is cleared each frame.
    CHECK(c.visible_class() == 2);
}

TEST_CASE("NMS plugin: the server's pet slots stay out of the extended target (zone/client_process.cpp XTargets[19 - i])") {
    Host h;
    net::GameConnection& c = h.c();
    REQUIRE(h.plugins->load("nms"));
    h.zone_in({nms::op::ClientReport, {}});
    auto& w = c.world_for_test();
    for (std::uint32_t id : {300u, 301u, 500u}) {
        game::Spawn s;
        s.id = id;
        s.name = id == 500 ? "a_rat00" : "Gobaner" + std::to_string(id);
        s.is_npc = true;
        s.pet_owner_id = id == 300 ? 1 : 0; // 301: a pet known only from OP_PetList
        w.spawns[id] = s;
    }
    // OP_PetList: the core keeps the list (focus, gauges, the pet window).
    CHECK_FALSE(h.zone_in(nms::encode_pet_list({{300, 13}, {301, 13}, {302, 0}})));
    REQUIRE(c.pets().list.size() == 3);
    CHECK(c.pets().focused == 300);
    // MultiPet's /pets: the spawns' names, "(spawning)" before the spawn is here, the summoning class.
    h.chat();
    CHECK(h.command("/pets"));
    CHECK(h.chat() == std::vector<std::string>{"You have 3 pet(s):", "  1: Gobaner300 [Magician] (pet window)",
                                               "  2: Gobaner301 [Magician]", "  3: (spawning)"});
    // OP_XTargetResponse {max, count, slot, status, id, name} as SendXTargetPacket(slot, pet) writes it.
    auto slot_packet = [](std::uint32_t slot, std::uint32_t id, const std::string& name) {
        std::vector<std::byte> b;
        auto put = [&](std::uint32_t v, int n) {
            for (int i = 0; i < n; ++i) b.push_back(static_cast<std::byte>(v >> (8 * i)));
        };
        put(20, 4);
        put(1, 4);
        put(slot, 4);
        put(1, 1);
        put(id, 4);
        for (char ch : name) b.push_back(static_cast<std::byte>(ch));
        b.push_back(std::byte{0});
        return net::AppPacket(net::to_wire(net::Op::XTargetResponse), std::move(b));
    };
    c.receive_zone_packet_for_test(slot_packet(19, 300, "Gobaner300")); // petids[0]
    c.receive_zone_packet_for_test(slot_packet(18, 301, "Gobaner301")); // petids[1]
    c.receive_zone_packet_for_test(slot_packet(0, 500, "a rat"));       // an auto hater
    REQUIRE(c.xtargets().slots.size() == 20);
    CHECK(c.xtarget_slot_hidden(19));
    CHECK(c.xtarget_slot_hidden(18));
    CHECK_FALSE(c.xtarget_slot_hidden(0));
    // The slot keys pass the hidden slots by; numbers stay the server's.
    c.target_xtarget(19);
    CHECK(c.world().target_id == 10);
    c.target_xtarget(0);
    CHECK(c.world().target_id == 500);
    // A slot the player sets to their own pet stays: the request makes it a specific NPC here.
    c.xtarget_request(5, game::kXTargetSpecificNpc, "Gobaner300");
    c.receive_zone_packet_for_test(slot_packet(5, 300, "Gobaner300"));
    CHECK_FALSE(c.xtarget_slot_hidden(5));
    // /petcycle shows the next pet in the pet window, round to the first.
    CHECK(h.command("/petcycle"));
    CHECK(c.pets().focused == 301);
    h.command("/petcycle");
    h.command("/petcycle");
    CHECK(c.pets().focused == 300);

    // The option: on by default; off shows them, and is kept in the plugin's nms.ini.
    REQUIRE(h.bridge.options().size() == 1);
    CHECK(h.bridge.options()[0].get() == 1);
    h.bridge.options()[0].set(0);
    CHECK_FALSE(c.xtarget_slot_hidden(19));
    std::ifstream ini(h.world.dir / "config" / "nms" / "nms.ini");
    std::string text((std::istreambuf_iterator<char>(ini)), std::istreambuf_iterator<char>());
    CHECK(text.find("xtarget_hide_pets=0") != std::string::npos);
}

TEST_CASE("NMS plugin: a saved server's [NMS] keys from builds before the plugin") {
    Host h;
    const fs::path profile = h.world.dir / "servers" / "nms";
    fs::create_directories(profile);
    std::ofstream(profile / "server.ini") << "[Server]\nname=NMS\n[NMS]\nauth_key=4242\n";
    REQUIRE(h.plugins->load("nms"));
    h.zone_in({nms::op::ClientReport, {}});
    h.sent.clear();
    h.hooks.zone(plugin::ZonePhase::Zoned);
    REQUIRE_FALSE(h.sent.empty());
    CHECK(h.sent[0].data == nms::encode_cauth(2, 1, 4242).data); // no mask yet: the profile's class (Cleric)
}

namespace {

bool has_window(const std::string& name) { return ImGui::FindWindowByName(name.c_str()) != nullptr; }
bool window_active(const std::string& name) {
    ImGuiWindow* w = ImGui::FindWindowByName(name.c_str());
    return w && w->Active;
}

// The packs' ImGui names for the windows (Modern: hud.Dialog's "<title>###modern.<screen>"; Gamepad: W.begin's name).
struct PackNames {
    const char* pack;
    const char* loot;
    const char* vault;
    const char* bag; // a vault bag's own window (the Gamepad pack draws the open bag inside the vault)
    const char* waypoints;
    const char* notes;
};

} // namespace

TEST_CASE("NMS plugin windows: Personal Loot, the vault and the waypoints in the Modern and Gamepad UIs") {
    const PackNames packs[] = {
        {"modern", "Personal Loot (1)###modern.NmsLootWnd", "Dimensional Vault###modern.NmsVaultWnd", "Backpack##bag_3", "Attune the Map###modern.WaypointsWnd",
         "Notes###modern.PlayerNotesWindow"},
        {"gamepad", "Personal Loot", "Dimensional Vault", nullptr, "Attune the Map", "Notes"},
    };
    for (const PackNames& names : packs) {
        const char* pack = names.pack;
        INFO(pack);
        Host h;
        net::GameConnection& c = h.c();
        const fs::path dir = h.world.dir;
        const fs::path src = fs::path(HAILSTONE_CLIENT_UI_DIR) / pack; // the client's own packs
        REQUIRE(fs::exists(src));
        fs::copy(src, dir / pack, fs::copy_options::recursive);
        platform::set_data_dir_for_tests(dir); // (the notes window writes notes.txt there)
        REQUIRE(h.plugins->load("nms"));

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, ht = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &ht);
        {
            ui::ChatView view;
            app::EqHost host;
            host.game = h.world.game.get();
            host.conn = &c;
            host.chat = &view;
            host.in_game = host.ui_ready = true;
            host.plugin_ui = &h.bridge;
            app::EqApi api(host);
            ui::lua::LuaUi lua(dir / pack, dir / "ui.txt");
            lua.add_eq_api([&](lua_State* L) { api.register_functions(L); });
            api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
            auto frames = [&](int n) {
                for (int i = 0; i < n; ++i) {
                    // The plugin's windows for this pack (ClientApp::sync_plugin_windows).
                    std::vector<ui::lua::LuaUi::Folder> folders;
                    for (const auto& [name, folder] : h.bridge.folders()) folders.push_back({name, folder / "ui" / pack});
                    lua.set_folders(std::move(folders));
                    h.hooks.pulse();
                    ImGuiIO& io = ImGui::GetIO();
                    io.DisplaySize = ImVec2(1600, 900);
                    io.DeltaTime = 1.0f / 60.0f;
                    api.begin_frame();
                    ImGui::NewFrame();
                    lua.frame();
                    ImGui::Render();
                }
            };
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            // A stock server: nothing.
            CHECK_FALSE(window_active(names.loot));
            // The NMS server: the loot list opens the window; the console shows per its saved setting.
            h.zone_in({nms::op::ClientReport, {}});
            nms::PersonalLootList l;
            nms::PersonalLootRow r;
            r.pending_id = 7;
            r.item_id = 3003;
            r.quantity = 2;
            r.sell_value = 1234;
            r.seconds_left = 300;
            r.item_name = "Rusty Dagger";
            r.npc_name = "a gnoll";
            l.rows.push_back(r);
            h.zone_in(nms::encode(l));
            h.zone_in(nms::encode(nms::LootSettings{1, 0, 300, false, true}));
            frames(5);
            CHECK_FALSE(has_window("UI errors"));
            CHECK(window_active(names.loot));
            // The vault: its lines (which the patch hides) open it; a bag's window opens.
            h.chat();
            c.ui_message("VAULTDATA|CLEAR");
            c.ui_message("VAULTDATA|ADD|1|3|17005|0|565|Backpack|13005:2:1000:0:Iron Ration|0|0|0|0|0|0|0|8");
            c.ui_message("VAULTDATA|ADD|1|5|1001|1|794|Spider Silk");
            c.ui_message("VAULTDATA|OPEN|1");
            CHECK(h.chat().empty()); // hidden
            h.call("vault_window", {plugin::UiValue::string("bag"), plugin::UiValue::integer(3)});
            auto vault = h.call("vault");
            REQUIRE(vault);
            REQUIRE(vault->field("slots"));
            CHECK(vault->field("slots")->field("3")); // the slot's key, the Lua integer 3 in the windows
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            CHECK(window_active(names.vault));
            if (names.bag) CHECK(window_active(names.bag));
            c.ui_message("VAULTDATA|OPEN|9");
            frames(2);
            CHECK_FALSE(has_window("UI errors"));
            // Merchant only says; Bank says, then opens the bank window with the player as its banker (0x1019a420)
            h.ran.clear();
            h.call("vault_say", {plugin::UiValue::string("#vault_merchant")});
            CHECK_FALSE(c.bank());
            h.call("vault_say", {plugin::UiValue::string("#vault_bank")});
            CHECK(contains(h.ran, "/say #vault_bank"));
            REQUIRE(c.bank());
            CHECK(c.bank()->banker_id == c.world().player_id);
            frames(2);
            CHECK_FALSE(has_window("UI errors"));
            c.close_bank();
            // Each tab draws.
            for (const char* tab : {"rules", "history", "settings", "loot"}) {
                CHECK(h.command(std::string("/nmsloot tab ") + tab));
                frames(2);
                CHECK_FALSE(has_window("UI errors"));
            }
            // The last drop decided: the list empties, the window stays up (no script error with nothing to show).
            h.zone_in(nms::encode(nms::PersonalLootList{}));
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            // The waypoints: each list shows the window; Travel sends OP_WaypointRequest.
            nms::WaypointList wl;
            wl.group_enabled = true;
            wl.entries.push_back({0, 7, true, "Blackburrow"});
            wl.entries.push_back({1, 8, false, "Greater Faydark"});
            CHECK_FALSE(h.zone_in(nms::encode(wl)));
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            CHECK(window_active(names.waypoints));
            auto wp = h.call("waypoints");
            REQUIRE(wp);
            CHECK(wp->field("serial")->i == 1);
            REQUIRE(wp->field("entries")->items.size() == 2);
            CHECK(wp->field("entries")->items[1].field("continent")->s == "Faydwer");
            CHECK(wp->field("continents")->items[1].s == "Faydwer"); // the patch's names by category
            CHECK(wp->field("entries")->items[0].field("zone")->s == "blackburrow");
            h.command("/waypoint"); // shows it again
            CHECK(h.call("waypoints")->field("serial")->i == 2);
            h.sent.clear();
            h.call("waypoint_request", {plugin::UiValue::integer(7), plugin::UiValue::boolean(false),
                                        plugin::UiValue::boolean(true), plugin::UiValue::boolean(false)});
            REQUIRE(h.sent.size() == 1);
            CHECK(h.sent[0].data == nms::encode(nms::WaypointRequest{7, false, true, false}).data);
            // Closing hides them.
            h.command("/nmsloot hide");
            h.call("vault_window", {plugin::UiValue::string("hide")});
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            CHECK_FALSE(window_active(names.vault));
            // The notes window (/note): one text the player edits.
            api.show("PlayerNotesWindow", true);
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            CHECK(window_active(names.notes));
            api.show("PlayerNotesWindow", false);
        }
        ImGui::DestroyContext(ctx);
        platform::set_data_dir_for_tests({});
    }
}

TEST_CASE("NMS plugin: the Pet window's #petcmd (the server's zone/gm_commands/petcmd.cpp)") {
    Host h;
    net::GameConnection& c = h.c();
    REQUIRE(h.plugins->load("nms"));
    auto before = h.call("pets");
    CHECK((!before || before->kind == plugin::UiValue::Kind::Nil)); // not before it sees an NMS server
    h.zone_in({nms::op::ClientReport, {}});
    auto& w = c.world_for_test();
    for (std::uint32_t id : {300u, 301u, 302u}) {
        game::Spawn s;
        s.id = id;
        s.name = "Pet" + std::to_string(id);
        s.is_npc = true;
        s.pet_owner_id = 1;
        s.hp_percent = 80;
        w.spawns[id] = s;
    }
    h.zone_in(nms::encode_pet_list({{300, 13}, {301, 11}, {302, 0}}));
    auto st = h.call("pets");
    REQUIRE(st);
    const auto& pets = st->field("pets")->items;
    REQUIRE(pets.size() == 3);
    CHECK(pets[0].field("word")->s == "mag");
    CHECK(pets[0].field("className")->s == "Magician");
    CHECK(pets[0].field("name")->s == "Pet300");
    CHECK(pets[0].field("hp")->i == 80);
    CHECK(pets[0].field("focused")->b);
    CHECK(pets[1].field("word")->s == "nec");
    CHECK(pets[2].field("word")->s == ""); // a charmed pet: no class to name
    CHECK(st->field("verbs")->items.size() == 18);
    CHECK(st->field("swarm")->fields.empty());

    // A button: "/say #petcmd <command> [on|off] <pets>".
    h.ran.clear();
    h.call("pet_command", {plugin::UiValue::string("attack"), plugin::UiValue::string(""), plugin::UiValue::string("mag")});
    h.call("pet_command", {plugin::UiValue::string("taunt"), plugin::UiValue::string("off"), plugin::UiValue::string("all")});
    // Refused: a word the server does not take, a state on a plain command, swarm pets' unsupported commands.
    h.call("pet_command", {plugin::UiValue::string("dance"), plugin::UiValue::string(""), plugin::UiValue::string("all")});
    h.call("pet_command", {plugin::UiValue::string("sit"), plugin::UiValue::string("on"), plugin::UiValue::string("all")});
    h.call("pet_command", {plugin::UiValue::string("follow"), plugin::UiValue::string(""), plugin::UiValue::string("swarm")});
    h.call("pet_command", {plugin::UiValue::string("attack"), plugin::UiValue::string(""), plugin::UiValue::string("xyz")});
    CHECK(h.ran == std::vector<std::string>{"/say #petcmd attack mag", "/say #petcmd taunt off all"});
    // Their echoes are hidden; the player's own #petcmd is not.
    h.chat();
    c.ui_message("You say, '#petcmd attack mag'");
    c.ui_message("You say, '#petcmd taunt off all'");
    c.ui_message("You say, '#petcmd attack mag'");
    CHECK(h.chat() == std::vector<std::string>{"You say, '#petcmd attack mag'"});

    // The swarm query: once a zone, its four answers hidden and kept; later ones shown and kept.
    h.ran.clear();
    h.call("pet_swarm_query");
    h.call("pet_swarm_query");
    CHECK(h.ran == std::vector<std::string>{"/say #petcmd hold ghold focus assist swarm"});
    c.ui_message("You say, '#petcmd hold ghold focus assist swarm'");
    c.ui_message("Swarm pet hold setting: ON");
    c.ui_message("Swarm pet ghold setting: OFF");
    c.ui_message("Swarm pet focus setting: OFF");
    c.ui_message("Swarm pet assist setting: ON");
    c.ui_message("Swarm pet hold setting: OFF");
    CHECK(h.chat() == std::vector<std::string>{"Swarm pet hold setting: OFF"});
    const auto after = h.call("pets");
    REQUIRE(after);
    const auto* sw = after->field("swarm");
    REQUIRE(sw);
    CHECK_FALSE(sw->field("hold")->b);
    CHECK(sw->field("assist")->b);
    CHECK_FALSE(sw->field("ghold")->b);

    // The focused pet's switches: OP_PetCommandState (Mob::SetHeld and the like, for focused_pet_id), passed on.
    CHECK(after->field("switches")->fields.empty()); // nothing lights until the server says
    CHECK(h.zone_in(nms::encode_pet_command_state(6, 1)));
    CHECK(h.zone_in(nms::encode_pet_command_state(5, 0)));
    const auto lit = h.call("pets");
    REQUIRE(lit);
    const auto* fs = lit->field("switches");
    REQUIRE(fs);
    REQUIRE(fs->field("hold"));
    CHECK(fs->field("hold")->b);
    REQUIRE(fs->field("taunt"));
    CHECK_FALSE(fs->field("taunt")->b);
    CHECK_FALSE(fs->field("assist")); // the server sends no state for it
    h.hooks.zone(plugin::ZonePhase::Zoned);
    CHECK(h.call("pets")->field("switches")->field("hold")); // kept: the new zone sends them before OnZoned
    h.ran.clear();
    h.call("pet_swarm_query");
    CHECK(h.ran.size() == 1);
}

TEST_CASE("NMS plugin windows: the Pet window covers the pack's on an NMS server, in the Modern and Gamepad UIs") {
    struct Pack {
        const char* pack;
        const char* window;    // its ImGui name
        const char* folded;    // the setting that shows the stock buttons
    };
    const Pack packs[] = {{"modern", "Pet###modern.PetInfoWindow", "nms.pet.commandsOpen"},
                          {"gamepad", "Pet", "nms.pet.stockCommands"}};
    for (const Pack& pk : packs) {
        INFO(pk.pack);
        Host h;
        net::GameConnection& c = h.c();
        const fs::path dir = h.world.dir;
        const fs::path src = fs::path(HAILSTONE_CLIENT_UI_DIR) / pk.pack; // the client's own packs
        fs::copy(src, dir / pk.pack, fs::copy_options::recursive);
        platform::set_data_dir_for_tests(dir);
        REQUIRE(h.plugins->load("nms"));
        auto& w = c.world_for_test();
        for (std::uint32_t id : {300u, 301u}) {
            game::Spawn s;
            s.id = id;
            s.name = "Pet" + std::to_string(id);
            s.is_npc = true;
            s.pet_owner_id = 1;
            w.spawns[id] = s;
        }

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char* px = nullptr;
        int tw = 0, th = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
        {
            ui::ChatView view;
            app::EqHost host;
            host.game = h.world.game.get();
            host.conn = &c;
            host.chat = &view;
            host.in_game = host.ui_ready = true;
            host.plugin_ui = &h.bridge;
            app::EqApi api(host);
            ui::lua::LuaUi lua(dir / pk.pack, dir / "ui.txt");
            lua.add_eq_api([&](lua_State* L) { api.register_functions(L); });
            api.load_layout(dir / "defaults.ini", dir / "UI_Test_Server.ini", {});
            auto frames = [&](int n) {
                for (int i = 0; i < n; ++i) {
                    std::vector<ui::lua::LuaUi::Folder> folders;
                    for (const auto& [name, folder] : h.bridge.folders()) folders.push_back({name, folder / "ui" / pk.pack});
                    lua.set_folders(std::move(folders));
                    h.hooks.pulse();
                    ImGuiIO& io = ImGui::GetIO();
                    io.DisplaySize = ImVec2(1920, 1080);
                    io.DeltaTime = 1.0f / 60.0f;
                    api.begin_frame();
                    ImGui::NewFrame();
                    lua.frame();
                    ImGui::Render();
                }
            };
            auto run = [&](const char* code) {
                REQUIRE(luaL_dostring(lua.state(), code) == LUA_OK);
            };
            api.show("PetInfoWindow", true);
            frames(3);
            if (std::string(pk.pack) == "gamepad") run("GPSTATE.openPet()");
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            // A stock server: the pack's own window (ui.base_window).
            REQUIRE(window_active(pk.window));
            const float stock_width = ImGui::FindWindowByName(pk.window)->Size.x;
            // The NMS server: the plugin's, wider, with the #petcmd part.
            h.zone_in({nms::op::ClientReport, {}});
            h.zone_in(nms::encode_pet_list({{300, 13}, {301, 11}}));
            frames(3);
            CHECK_FALSE(has_window("UI errors"));
            REQUIRE(window_active(pk.window));
            CHECK(ImGui::FindWindowByName(pk.window)->Size.x > stock_width);
            // The stock buttons shown, then rolled up again.
            run((std::string("ui.SetSetting('") + pk.folded + "', true)").c_str());
            frames(2);
            CHECK_FALSE(has_window("UI errors"));
            run((std::string("ui.SetSetting('") + pk.folded + "', false)").c_str());
            frames(2);
            CHECK_FALSE(has_window("UI errors"));
            CHECK(window_active(pk.window));
        }
        ImGui::DestroyContext(ctx);
        platform::set_data_dir_for_tests({});
    }
}
