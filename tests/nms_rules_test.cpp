// The NMS plugin's rules (plugins/nms/nms_rules.h), packets (nms_packets.h), windows' state (nms_loot.h,
// nms_vault.h, nms_waypoints.h) and settings (nms_options.h), built into the tests from the plugin's own sources,
// against rof2decomp's static analysis of NMS/dinput8.dll (notes/nms-dinput8.md) and the NMS server's structs. The
// plugin itself, loaded into a client: nms_plugin_test.cpp.

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "nmsc_byte_io.h"
#include "nms_ini.h"
#include "nms_loot.h"
#include "nms_options.h"
#include "nms_packets.h"
#include "nms_petcmd.h"
#include "nms_rules.h"
#include "nms_vault.h"
#include "nms_waypoints.h"
#include "scratch_dir.h"

namespace {

// Minor Healing's level bytes: Cleric 1, Paladin 9, Druid 1, Shaman 1, Ranger 9 ... the rest 255.
std::array<std::uint8_t, 16> minor_healing() {
    std::array<std::uint8_t, 16> l{};
    l.fill(255);
    l[1] = 1;  // Cleric
    l[2] = 9;  // Paladin
    l[3] = 9;  // Ranger
    l[5] = 1;  // Druid
    l[9] = 1;  // Shaman
    l[14] = 9; // Beastlord
    return l;
}

nms::LevelOf of(const std::array<std::uint8_t, 16>& l) {
    return [l](std::uint32_t c) { return l[(c >= 1 && c <= 16 ? c : 1) - 1]; };
}

constexpr std::uint32_t bit(std::uint32_t c) { return 1u << (c - 1); }

std::uint32_t u32_at(const std::vector<std::byte>& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | std::to_integer<std::uint32_t>(b[at + static_cast<std::size_t>(i)]);
    return v;
}
std::uint64_t u64_at(const std::vector<std::byte>& b, std::size_t at) {
    return u32_at(b, at) | static_cast<std::uint64_t>(u32_at(b, at + 4)) << 32;
}

} // namespace

TEST_CASE("NMS GetSpellLevelNeeded detour (notes §3.2)") {
    auto orig = of(minor_healing());
    const std::uint32_t clr = bit(2), war_wiz = bit(1) | bit(12), clr_pal = bit(2) | bit(3);
    // Mask 0 or no player: the original.
    CHECK(nms::spell_level_needed(orig, 8, 8, 0, true, false) == 255);
    CHECK(nms::spell_level_needed(orig, 2, 8, clr, false, false) == 1);
    // The visible class (8) and the mask's classes: the mask minimum.
    CHECK(nms::spell_level_needed(orig, 8, 8, clr, true, false) == 1);
    CHECK(nms::spell_level_needed(orig, 3, 8, clr_pal, true, false) == 1);
    CHECK(nms::spell_level_needed(orig, 8, 8, war_wiz, true, false) == 255);
    // Any other class: 255, even one that can use it.
    CHECK(nms::spell_level_needed(orig, 6, 8, clr, true, false) == 255);
    // Scribing: 1 when usable through the mask or by arg itself, else 255.
    CHECK(nms::spell_level_needed(orig, 8, 8, clr, true, true) == 1);
    CHECK(nms::spell_level_needed(orig, 8, 8, war_wiz, true, true) == 255);
    CHECK(nms::spell_level_needed(orig, 6, 8, war_wiz, true, true) == 1);
    CHECK(nms::spell_level_needed(orig, 1, 8, 0, true, true) == 255);
    CHECK(nms::mask_min_level(orig, 0) == 255);
}

TEST_CASE("NMS class rules (notes §3.1, §3.3, §3.8)") {
    CHECK(nms::is_spellcaster(bit(2)));
    CHECK(nms::is_spellcaster(bit(8)));
    CHECK_FALSE(nms::is_spellcaster(bit(1) | bit(7) | bit(9) | bit(16)));
    // GetUsableClasses for the merchant filter.
    CHECK(nms::usable_classes(0, bit(2), 8) == 0xffff);
    CHECK(nms::usable_classes(bit(2) | bit(8), bit(2), 8) == 0xffff);
    CHECK(nms::usable_classes(bit(1) | bit(8), bit(2), 8) == bit(1));
    CHECK(nms::usable_classes(bit(8), bit(2), 8) == 1);
    CHECK(nms::bard_cast_bard_blocked(255));
    CHECK_FALSE(nms::bard_cast_bard_blocked(1));
    CHECK(nms::can_use_memorized_spell_slot(11));
    CHECK_FALSE(nms::can_use_memorized_spell_slot(12));
    // The server's own 12205 through EQEmu's FilterPCSpells check: 0 show, 1 hide, else group only.
    CHECK_FALSE(nms::own_begin_cast_dropped(0, false, false));
    CHECK(nms::own_begin_cast_dropped(1, true, false));
    CHECK(nms::own_begin_cast_dropped(2, false, false));
    CHECK_FALSE(nms::own_begin_cast_dropped(2, true, false));
    CHECK_FALSE(nms::own_begin_cast_dropped(2, false, true));
    // The cast spoof: the first mask class with a level 1..254.
    auto orig = of(minor_healing());
    CHECK(nms::cast_spoof_class(orig, bit(1) | bit(6) | bit(2)) == 2);
    CHECK(nms::cast_spoof_class(orig, bit(1) | bit(12)) == 0);
    // Skills.
    CHECK(nms::skill_spoof_class(8, bit(9) | bit(2)) == 9);
    CHECK(nms::skill_spoof_class(30, bit(16) | bit(7)) == 7); // Kick: Warrior, Monk, Ranger, Beastlord, Berserker
    CHECK(nms::skill_spoof_class(10, bit(2) | bit(5)) == 5);  // Bash: Warrior, Paladin, Shadow Knight, Cleric
    CHECK(nms::skill_spoof_class(74, bit(1)) == 0);
    CHECK(nms::skill_spoof_class(73, bit(4)) == 4);
    CHECK(nms::skill_spoof_class(26, bit(7)) == 7);
    CHECK(nms::skill_spoof_class(0, 0xffff) == 0);
    CHECK(nms::skill_gate(nms::kSkillTracking, bit(2), 50) == false);
    CHECK_FALSE(nms::skill_gate(nms::kSkillTracking, bit(6), 50).has_value());
    CHECK_FALSE(nms::skill_gate(nms::kSkillTracking, 0, 50).has_value());
    CHECK(nms::skill_gate(30, bit(1), -1) == false);
}

TEST_CASE("NMS displays (notes §3.6, §3.7, §3.10)") {
    CHECK(nms::hide_song_ends(bit(2)));
    CHECK_FALSE(nms::hide_song_ends(bit(2) | bit(8)));
    CHECK_FALSE(nms::hide_song_ends(0));
    auto orig = of(minor_healing());
    CHECK(nms::level_needed_line(orig, bit(2) | bit(3), 8) == "Level Needed: CLR(1) PAL(9)");
    CHECK(nms::level_needed_line(orig, bit(1), 8) == "Level Needed: CANNOT USE");
    CHECK(nms::level_needed_line(orig, 0, 2) == "Level Needed: CLR(1)");
    CHECK(nms::list_level(orig, bit(3)) == 9);
    CHECK(nms::list_level(orig, bit(1)) == 1); // no mask class: the minimum over all 16
    std::array<std::uint8_t, 16> none{};
    none.fill(255);
    CHECK_FALSE(nms::list_level(of(none), bit(1)).has_value());
}

TEST_CASE("NMS /who reformat (FUN_10198eb0)") {
    auto zone = [](std::uint32_t id) { return id == 1 ? std::string("qeynos") : std::string(); };
    nms::WhoEntry a;
    a.format = 5022;
    a.name = "Morsal";
    a.rank_string = 5012;
    a.zone_string = 5006;
    a.zone_id = 1;
    a.class_value = bit(10) | bit(11) | bit(13);
    a.level = 100;
    a.race = 128;
    nms::WhoEntry b;
    b.format = 5024;
    b.name = "Hidden";
    auto lines = nms::format_who({a, b}, zone);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "[Morsal] - [100 SHM/NEC/MAG (Iksar)] ZONE: qeynos * GM * ");
    CHECK(lines[1] == "[Hidden] - [ANONYMOUS]");
    CHECK(lines[2] == "There are 2 players in EverQuest.");
    // A single-bit value is read as a class id when it is 1..16 (the DLL's ambiguity).
    a.class_value = 8;
    a.zone_string = 0xffffffff;
    a.rank_string = 0xffffffff;
    CHECK(nms::format_who({a}, zone)[0] == "[Morsal] - [100 BRD (Iksar)]");
    CHECK(nms::format_who({a}, zone)[1] == "There is 1 player in EverQuest.");
    CHECK(nms::format_who({}, zone)[0] == "There are no players in EverQuest that match those who filters.");
}

TEST_CASE("NMS discipline timers >= 20") {
    nms::DisciplineTimers t;
    CHECK_FALSE(nms::DisciplineTimers::handles(19));
    CHECK(nms::DisciplineTimers::handles(20));
    CHECK(t.get(25) == 0);
    t.set(25, 1000);
    t.set(25, 1100);
    CHECK(t.get(25) == 1100);
    CHECK(t.remaining(25, 1000) == 100);
    CHECK(t.ready(25, 1100));
    CHECK_FALSE(t.ready(25, 1099));
}

TEST_CASE("NMS packets (notes §4.2)") {
    // 0x140f: 34 bytes on the wire = 32 of payload.
    auto rep = nms::encode_client_report(nms::kReportLargeAddress);
    CHECK(rep.opcode == 0x140f);
    REQUIRE(rep.data.size() == 32);
    CHECK(u32_at(rep.data, 0) == 0x5ab3e769);
    CHECK(u32_at(rep.data, 4) == 4);
    for (std::size_t i = 8; i < 32; ++i) CHECK(rep.data[i] == std::byte{0});
    // OP_CAuth: 262 bytes on the wire; 8 XORed hash bytes, 248 zeros, u32 0.
    const std::uint32_t key = 0x11223344;
    auto ca = nms::encode_cauth(2, 160, key);
    CHECK(ca.opcode == nms::op::CAuth);
    REQUIRE(ca.data.size() == 260);
    std::uint64_t h = 0;
    for (int i = 7; i >= 0; --i) {
        auto k = static_cast<std::uint8_t>(key >> (8 * (i % 4)));
        h = h << 8 | static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(ca.data[static_cast<std::size_t>(i)]) ^ k);
    }
    CHECK(h == 320);
    for (std::size_t i = 8; i < 260; ++i) CHECK(ca.data[i] == std::byte{0});
    // The hardware-id rewrite: the VM values, never real ids; None leaves the packet alone.
    std::vector<std::byte> crc1(2056, std::byte{0x5a}), crc2 = crc1, crc3 = crc1;
    auto keep = crc1;
    CHECK_FALSE(nms::apply_hardware_id(nms::op::World_Client_CRC1, keep, nms::HardwareIdMode::None));
    CHECK(keep == crc1);
    CHECK(nms::apply_hardware_id(nms::op::World_Client_CRC1, crc1, nms::HardwareIdMode::Fixed));
    CHECK(u64_at(crc1, 0) == 999999999999ull);
    CHECK(crc1[8] == std::byte{0x5a});
    CHECK(nms::apply_hardware_id(nms::op::World_Client_CRC2, crc2, nms::HardwareIdMode::Fixed));
    CHECK(u64_at(crc2, 0) == 99999999ull);
    CHECK_FALSE(nms::apply_hardware_id(0x298d, crc3, nms::HardwareIdMode::Fixed)); // OP_World_Client_CRC3
    std::vector<std::byte> shorter(7, std::byte{0x5a});
    CHECK_FALSE(nms::apply_hardware_id(nms::op::World_Client_CRC1, shorter, nms::HardwareIdMode::Fixed));
    CHECK(nms::parse_hardware_id_mode("fixed") == nms::HardwareIdMode::Fixed);
    CHECK(nms::parse_hardware_id_mode("none") == nms::HardwareIdMode::None);
    CHECK_FALSE(nms::parse_hardware_id_mode("mac").has_value());
}

TEST_CASE("NMS server packets round trip") {
    nms::SkillTimers t{1, 2, 3, 4, 5, 0x30};
    auto p = nms::encode_skill_timers(t);
    CHECK(p.data.size() == 28); // EQEmu: 4 + sizeof(SkillTimers_Struct)
    auto d = nms::decode_skill_timers(p.data);
    REQUIRE(d);
    CHECK(d->kick == 3);
    CHECK(d->autoskill == 0x30);

    nms::WaypointList wl;
    wl.force_show = true;
    wl.entries.push_back({2, 7, true, "The Plane of Knowledge"});
    auto wp = nms::encode(wl);
    CHECK(wp.data.size() == 9 + 73); // packed WaypointList_Struct + one entry
    auto wd = nms::decode_waypoint_list(wp.data);
    REQUIRE(wd);
    CHECK(wd->force_show);
    REQUIRE(wd->entries.size() == 1);
    CHECK(wd->entries[0].id == 7);
    CHECK(wd->entries[0].name == "The Plane of Knowledge");
    CHECK(nms::encode(nms::WaypointRequest{7, false, true, false}).data.size() == 7);


    auto st = nms::encode_server_auth_stats({{1, 0x2}, {2, 100}});
    CHECK(st.opcode == nms::op::ServerAuthStats);
    CHECK(st.data.size() == 4 + 24);
    auto sd = nms::decode_server_auth_stats(st.data);
    REQUIRE(sd);
    CHECK((*sd)[0] == std::pair<std::uint32_t, std::int64_t>{1, 2});
    st.data.push_back(std::byte{0});
    CHECK_FALSE(nms::decode_server_auth_stats(st.data).has_value());

    // OP_DisciplineTimer {id, seconds, u32}: the first two.
    std::vector<std::byte> dt(12);
    dt[0] = std::byte{25};
    dt[4] = std::byte{90};
    CHECK(nms::decode_discipline_timer(dt) == std::pair<std::uint32_t, std::uint32_t>{25, 90});
    dt.resize(7);
    CHECK_FALSE(nms::decode_discipline_timer(dt));

    // OP_PetList.
    auto pl = nms::encode_pet_list({{300, 13}, {301, 0}});
    CHECK(pl.opcode == nms::op::PetList);
    auto pd = nms::decode_pet_list(pl.data);
    REQUIRE(pd);
    REQUIRE(pd->size() == 2);
    CHECK((*pd)[1].spawn_id == 301);
    pl.data.resize(10);
    CHECK_FALSE(nms::decode_pet_list(pl.data));

    // OP_SendCharacterSets: 4396 bytes of sets, then 332 per character.
    nms::CharacterSets cs;
    cs.selected_set = cs.default_set = 7;
    cs.sets.push_back({7, "Default"});
    cs.characters.push_back({55, "Gennro", 60, 0x8001, {7}});
    auto cp = nms::encode(cs);
    CHECK(cp.opcode == nms::op::SendCharacterSets);
    CHECK(cp.data.size() == 4396 + 332);
    auto cd = nms::decode_character_sets(cp.data);
    REQUIRE(cd);
    REQUIRE(cd->characters.size() == 1);
    CHECK(cd->characters[0].name == "Gennro");
    CHECK(cd->characters[0].classes == 0x8001);
    CHECK(cd->characters[0].sets == std::vector<std::uint32_t>{7});
    REQUIRE(cd->sets.size() == 1);
    CHECK(cd->sets[0].name == "Default");
    cp.data.pop_back();
    CHECK_FALSE(nms::decode_character_sets(cp.data));

    // OP_PersonalLootList and the action.
    nms::PersonalLootList ll;
    ll.flags = nms::kLootListShow;
    nms::PersonalLootRow lr;
    lr.pending_id = 7;
    lr.item_name = "Rusty Dagger";
    ll.rows.push_back(lr);
    auto lp = nms::encode(ll);
    CHECK(lp.data.size() == 8 + 160);
    auto ld = nms::decode_personal_loot_list(lp.data);
    REQUIRE(ld);
    CHECK(ld->rows[0].item_name == "Rusty Dagger");
    auto la = nms::encode_personal_loot_action(nms::kLootActionKeep, 9);
    CHECK(la.opcode == nms::op::PersonalLootAction);
    CHECK(la.data.size() == 16);
    CHECK(u32_at(la.data, 0) == 1);
    CHECK(u32_at(la.data, 4) == 9);
}

TEST_CASE("NMS waypoint window: continents and expansions") {
    // The server's categories (nms_waypoints_categories), not the patch's table (Discord at 7, Special at 8).
    CHECK(std::string(nms::continent_name(7)) == "Taelosia");
    CHECK(std::string(nms::continent_name(8)) == "Discord");
    CHECK(std::string(nms::continent_name(9)) == "Unknown");
    CHECK(std::string(nms::expansion_name(4)) == "Planes of Power");
    CHECK(std::string(nms::expansion_name(-1)) == "Unknown");

    // From nms_waypoints.inc: zone.expansion, where it differs from the continent (Stonebrunt is on Odus).
    CHECK(nms::waypoint_expansion({2, 34, true, "Stonebrunt Mountains"}) == 2);
    CHECK(nms::waypoint_expansion({2, 35, true, "Dulak Harbor"}) == 5);
    CHECK(nms::waypoint_zone({2, 35, true, "Dulak Harbor"}) == "dulak");
    // The server marks the player's own zone.
    CHECK(nms::is_current_zone("The Bazaar [Current Zone]"));
    CHECK(nms::waypoint_expansion({5, 67, false, "The Bazaar [Current Zone]"}) == 3);
    // A waypoint the table does not know: its continent's expansion; The Planes span several.
    CHECK(nms::waypoint_expansion({3, 999, true, "Somewhere New"}) == 1);
    CHECK(nms::waypoint_expansion({6, 998, true, "Another Plane"}) == -1);
}

TEST_CASE("NMS vitals detours: Max / Cur HP, mana and endurance (notes §2c, EQCharacter1Hook)") {
    // Keys 2..7 when present, whatever else is in the map; the int64 against 0x7ffffffe, signed.
    std::map<std::uint32_t, std::int64_t> stats{{1, 0x82}, {2, 2775}, {5, 3100}, {6, 0}, {7, 0x1'0000'0000LL}};
    nms::Vitals v = nms::server_vitals(stats);
    CHECK(v.cur_hp == 2775);
    CHECK(v.max_hp == 3100);
    CHECK(v.max_mana == 0); // present: 0 is answered too
    CHECK(v.max_endurance == 0x7ffffffe);
    CHECK_FALSE(v.cur_mana);
    CHECK_FALSE(v.cur_endurance);
    stats = {{3, 0x7ffffffeLL}, {4, -5}, {6, 0x7ffffffdLL}};
    v = nms::server_vitals(stats);
    CHECK(v.cur_mana == 0x7ffffffe);
    CHECK(v.cur_endurance == -5); // the low 32 bits: no lower clamp
    CHECK(v.max_mana == 0x7ffffffd);
    const nms::Vitals none = nms::server_vitals({});
    CHECK_FALSE(none.max_hp);
    CHECK_FALSE(none.cur_hp);
    CHECK_FALSE(none.max_mana);
    CHECK_FALSE(none.cur_mana);
    CHECK_FALSE(none.max_endurance);
    CHECK_FALSE(none.cur_endurance);
}

TEST_CASE("NMS options (nms.ini)") {
    nms::Ini global, profile, server;
    global.parse("[NMS]\nauth_key=11\nhardware_id=none\nxtarget_hide_pets=0\n");
    nms::Options o = nms::read_options({&global});
    CHECK(o.auth_key == 11);
    CHECK(o.hardware_id == nms::HardwareIdMode::None);
    CHECK_FALSE(o.xtarget_hide_pets);
    // A saved server's server.ini [NMS] (as builds before the plugin kept it), then its own nms.ini.
    profile.parse("[Server]\nname=NMS\n[NMS]\nauth_key=4242\nhardware_id=fixed\n");
    o = nms::read_options({&global, &profile});
    CHECK(o.auth_key == 4242);
    CHECK(o.hardware_id == nms::HardwareIdMode::Fixed);
    CHECK_FALSE(o.xtarget_hide_pets);
    server.parse("[NMS]\nauth_key=0\nhardware_id=mac\n");
    std::vector<std::string> warnings;
    o = nms::read_options({&global, &profile, &server}, &warnings);
    CHECK(o.auth_key == 0); // 0 turns the OP_CAuth answer off
    CHECK(o.hardware_id == nms::HardwareIdMode::Fixed); // the unknown value keeps the one before
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("mac") != std::string::npos);
    // Without a key the current patch's own.
    CHECK(nms::read_options({}).auth_key == nms::kPatchAuthKey);
    CHECK(nms::kPatchAuthKey == 352236586u);
    CHECK(nms::read_options({nullptr}).xtarget_hide_pets);
}

TEST_CASE("NMS ini files") {
    nms::Ini ini;
    ini.parse("; a comment\n[AlwaysKeep]\n1001 = Spider Silk\nbad line\n[alwayskeep]\n13005=Rat Ears\n[AlwaysSell]\n");
    CHECK(ini.keys("ALWAYSKEEP") == std::vector<std::string>{"1001", "13005"}); // sections and keys: any case
    CHECK(ini.get_string("AlwaysKeep", "1001") == "Spider Silk");
    CHECK_FALSE(ini.get("AlwaysSell", "1"));
    ini.set("AlwaysKeep", "1001", "Silk");
    ini.set("New", "k", "v");
    CHECK(ini.to_string() == "[AlwaysKeep]\n1001=Silk\n13005=Rat Ears\n\n[AlwaysSell]\n\n[New]\nk=v\n");
    nmsc::test::ScratchDir dir("nms_ini_test");
    std::filesystem::create_directories(dir.path());
    REQUIRE(ini.save(dir.path() / "x.ini"));
    nms::Ini again;
    REQUIRE(again.load(dir.path() / "x.ini"));
    CHECK(again.get_string("New", "k") == "v");
    CHECK_FALSE(again.load(dir.path() / "missing.ini"));
    CHECK(again.keys("New").empty());
}

TEST_CASE("NMS Personal Loot snapshots round trip (common/eq_packet_structs.h)") {
    std::vector<nms::LootFilterRow> f{{13005, 794, 4, 3, "Rat Ears"}};
    auto fp = nms::encode(f);
    CHECK(fp.data.size() == 4 + 80);
    auto fd = nms::decode_loot_filters(fp.data);
    REQUIRE(fd);
    REQUIRE(fd->size() == 1);
    CHECK((*fd)[0].name == "Rat Ears");
    CHECK((*fd)[0].rule == 3);
    nms::LootSettings s{1, 250, 300, true, false};
    auto sp = nms::encode(s);
    CHECK(sp.data.size() == 16);
    auto sd = nms::decode_loot_settings(sp.data);
    REQUIRE(sd);
    CHECK(sd->sell_under == 250);
    CHECK(sd->timeout_seconds == 300);
    CHECK(sd->defer_in_combat);
    CHECK_FALSE(sd->report_enabled);
    std::vector<nms::LootHistoryRow> h{{1001, 500, 2, 8, 30, 2, "Spider Silk", "a spider"}};
    auto hp = nms::encode(h);
    CHECK(hp.data.size() == 4 + 152);
    auto hd = nms::decode_loot_history(hp.data);
    REQUIRE(hd);
    CHECK((*hd)[0].npc_name == "a spider");
    CHECK((*hd)[0].copper == 8);
    // A count beyond the payload is refused.
    hp.data.resize(100);
    CHECK_FALSE(nms::decode_loot_history(hp.data));
    CHECK(nms::loot_money(0) == "-");
    CHECK(nms::loot_money(1234) == "1p 2g 3s 4c");
    CHECK(nms::loot_money(1000) == "1p");
    CHECK(nms::loot_money(5) == "5c");
}

TEST_CASE("NMS Personal Loot: rules answer rows, the window opens for the rest (loot_window.cpp OnList)") {
    namespace fs = std::filesystem;
    nmsc::test::ScratchDir dir_scratch("nms_loot_test");
    fs::path dir = dir_scratch.path();
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "itemrules.ini");
        out << "[AlwaysKeep]\n1001=Spider Silk\n13005=Rat Ears\n[AlwaysSell]\n13005=Rat Ears\n[SellOnce]\n2002=Bone Chips\n";
    }
    nms::PersonalLoot loot;
    loot.rules().load(dir / "itemrules.ini");
    REQUIRE(loot.rules().rules().size() == 3);
    CHECK(loot.rules().get(13005) == nms::LootRule::AlwaysSell); // the last section wins
    auto row = [](std::uint32_t pending, std::uint32_t item, const char* name) {
        nms::PersonalLootRow r;
        r.pending_id = pending;
        r.item_id = item;
        r.quantity = 1;
        r.seconds_left = 300;
        r.item_name = name;
        r.npc_name = "a rat";
        return r;
    };
    nms::PersonalLootList l;
    l.rows = {row(1, 1001, "Spider Silk"), row(2, 2002, "Bone Chips"), row(3, 3003, "Rusty Dagger")};
    auto sends = loot.on_list(l, 1000);
    REQUIRE(sends.size() == 2);
    CHECK(sends[0] == nms::LootSend{nms::kLootActionKeep, 1, 0, 0});
    CHECK(sends[1] == nms::LootSend{nms::kLootActionSell, 2, 0, 0});
    CHECK(loot.rules().get(2002) == nms::LootRule::None); // a Once rule goes when it fires, and the file with it
    {
        nms::LootRules again;
        again.load(dir / "itemrules.ini");
        CHECK(again.get(2002) == nms::LootRule::None);
        CHECK(again.get(1001) == nms::LootRule::AlwaysKeep);
    }
    CHECK(loot.visible()); // the dagger needs a decision
    CHECK(loot.take_requested_tab() == 0);
    CHECK(loot.take_beep());
    CHECK(loot.visible_count(1000) == 1);
    CHECK(loot.local_history().size() == 2);
    CHECK(nms::PersonalLoot::seconds_left(loot.rows()[2], 61000) == 240);
    // The server drops the answered rows; the player keeps the dagger: the window closes.
    l.rows = {row(3, 3003, "Rusty Dagger")};
    CHECK(loot.on_list(l, 2000).empty());
    CHECK(loot.visible());
    l.rows.clear();
    loot.on_list(l, 3000);
    CHECK_FALSE(loot.visible());
    // A row answered by a rule that the server keeps (full inventory) shows again after 10 s.
    l.rows = {row(4, 1001, "Spider Silk")};
    CHECK(loot.on_list(l, 4000).size() == 1);
    CHECK(loot.visible_count(5000) == 0);
    CHECK(loot.visible_count(14001) == 1);
    // Show on an empty list keeps it open; camping hides it.
    l.rows.clear();
    l.flags = nms::kLootListShow;
    loot.on_list(l, 15000);
    CHECK(loot.visible());
    CHECK(loot.forced_open());
    l.flags = nms::kLootListCamping;
    loot.on_list(l, 16000);
    CHECK_FALSE(loot.visible());
    // The Hello: 3 s after the player exists, once per zone.
    CHECK_FALSE(loot.pulse(false, 0));
    CHECK_FALSE(loot.pulse(true, 100));
    CHECK_FALSE(loot.pulse(true, 3000));
    auto hello = loot.pulse(true, 3100);
    REQUIRE(hello);
    CHECK(hello->action == nms::kLootActionHello);
    CHECK_FALSE(loot.pulse(true, 9000));
    loot.reset(false);
    CHECK_FALSE(loot.pulse(true, 10000));
    CHECK(loot.pulse(true, 13000));
    fs::remove_all(dir);
}

TEST_CASE("NMS /loot (LootCommand)") {
    nms::PersonalLoot loot;
    auto r = nms::loot_command(loot, "", 0);
    CHECK(loot.visible());
    CHECK(r.lines.empty());
    r = nms::loot_command(loot, "hide", 0);
    CHECK_FALSE(loot.visible());
    r = nms::loot_command(loot, "show", 0);
    REQUIRE(r.sends.size() == 1);
    CHECK(r.sends[0].action == nms::kLootActionRefresh);
    r = nms::loot_command(loot, "tab history", 0);
    CHECK(loot.take_requested_tab() == 2);
    r = nms::loot_command(loot, "pin", 0);
    CHECK(loot.prefs.pinned);
    CHECK(r.lines == std::vector<std::string>{"[Loot] Window pinned."});
    r = nms::loot_command(loot, "rule 42 keeponce", 0); // no file yet: kept in memory
    CHECK(loot.rules().get(42) == nms::LootRule::KeepOnce);
    CHECK(r.lines == std::vector<std::string>{"[Loot] Item 42: Keep Once"});
    r = nms::loot_command(loot, "rule 42 bogus", 0);
    CHECK(r.lines == std::vector<std::string>{"[Loot] /loot rule <item id> <keep|sell|keeponce|sellonce|none>"});
    r = nms::loot_command(loot, "sellall", 0);
    CHECK(r.sends[0].action == nms::kLootActionSellAll);
    r = nms::loot_command(loot, "reset", 0);
    CHECK(r.reset_window);
}

TEST_CASE("NMS dsp_chat drops (FUN_10119d80)") {
    CHECK(nms::hide_patch_line("VAULTDATA|ADD|1|2|3"));
    CHECK(nms::hide_patch_line("--- DIMENSIONAL POCKET VAULT ---"));
    CHECK(nms::hide_patch_line("Slot 3: <Empty>"));
    CHECK_FALSE(nms::hide_patch_line("Slot 3"));
    CHECK(nms::hide_patch_line("You say, '#autoskill Kick enable'"));
    CHECK_FALSE(nms::hide_patch_line("Bob says, '#autoskill Kick enable'"));
    CHECK_FALSE(nms::hide_patch_line("Auto-skill Kick (30) enabled."));
}

TEST_CASE("NMS vault: the VAULTDATA lines (FUN_10199a20, FUN_1019bed0)") {
    nms::Vault v;
    CHECK_FALSE(v.on_chat("You say, 'hello'"));
    CHECK(v.on_chat("VAULTDATA|ADD|1|3|1001|1|794|Spider Silk"));
    REQUIRE(v.at(3));
    CHECK(v.at(3)->item_id == 1001);
    CHECK(v.at(3)->icon == 794);
    CHECK(v.at(3)->bag_size == 0);
    CHECK(v.on_chat("VAULTDATA|ADD|1|3|17005|0|565|Backpack|13005:2:1000:0:Iron Ration|0|0|0|0|0|0|1|8"));
    REQUIRE(v.slots().size() == 1); // the same (f0, f1) replaces
    const nms::VaultSlot* s = v.at(3);
    CHECK(s->name == "Backpack");
    CHECK(s->bag_size == 8);
    CHECK(s->flag13);
    REQUIRE(s->contents.size() == 1);
    CHECK(s->contents[0].quantity == 2);
    v.toggle_bag(*s);
    REQUIRE(v.bags().at(3).open);
    // NEST adds to the contents, with "~" between; the open bag window follows.
    CHECK(v.on_chat("VAULTDATA|NEST|1|3|13073:1:1001:4:Bone Chips~bad"));
    REQUIRE(v.at(3)->contents.size() == 2);
    CHECK(v.at(3)->contents[1].bag_slot == 4);
    CHECK(v.bags().at(3).contents.size() == 2);
    CHECK(v.at(3)->contents_text == "13005:2:1000:0:Iron Ration~13073:1:1001:4:Bone Chips~bad");
    // Too few fields: ignored. OPEN shows the page (at least 1).
    CHECK(v.on_chat("VAULTDATA|ADD|1|4|5|6"));
    CHECK_FALSE(v.at(4));
    CHECK_FALSE(v.shown());
    CHECK(v.on_chat("[NMS] VAULTDATA|OPEN|0"));
    CHECK(v.shown());
    CHECK(v.page() == 1);
    CHECK(v.on_chat("VAULTDATA|OPEN|9"));
    CHECK(v.page() == 9);
    CHECK(nms::vault_page_first(9) == 81);
    CHECK(nms::vault_page_last(9) == 83);
    CHECK(std::string(nms::vault_page_label(7)) == "Clicky 1");
    CHECK(v.on_chat("VAULTDATA|CLEAR"));
    CHECK(v.slots().empty());
}

TEST_CASE("NMS extended target: the server's pet slots are hidden (zone/client_process.cpp XTargets[19 - i])") {
    // The rule: an auto hater (or Empty Target) slot here holding one of the player's pets.
    std::vector<std::uint32_t> list{301, 302};
    CHECK(nms::xtarget_server_pet_slot(1, 300, 7, 7, {}));    // owner is the player
    CHECK(nms::xtarget_server_pet_slot(0, 302, 0, 7, list));  // on the server's pet list
    CHECK_FALSE(nms::xtarget_server_pet_slot(3, 300, 7, 7, list)); // the player set it: a specific NPC
    CHECK_FALSE(nms::xtarget_server_pet_slot(23, 300, 7, 7, list)); // the Pet role
    CHECK_FALSE(nms::xtarget_server_pet_slot(1, 400, 9, 7, list));  // someone else's pet
    CHECK_FALSE(nms::xtarget_server_pet_slot(1, 7, 0, 7, list));    // the player
    CHECK_FALSE(nms::xtarget_server_pet_slot(1, 0, 7, 7, list));    // no spawn
}

TEST_CASE("NMS OP_SendCharacterSets decodes as the NMS world writes it") {
    nmsc::ByteWriter w;
    for (std::uint32_t x : {7u, 7u, 2u, 1u, 4u, 12u, 1u, 100u, 200u, 0u, 1u}) w.le(x);
    for (std::uint32_t i = 0; i < 64; ++i) {
        w.le<std::uint32_t>(i < 2 ? 7 + i : 0);
        w.fixed_string(i == 0 ? "Default" : (i == 1 ? "Alts" : ""), 64);
    }
    w.le<std::uint32_t>(55);
    w.fixed_string("Gennro", 64);
    w.le<std::uint32_t>(60);
    w.le<std::uint32_t>(0x8001);
    for (std::uint32_t i = 0; i < 64; ++i) w.le<std::uint32_t>(i == 0 ? 7 : 0);
    const std::vector<std::byte> bytes = w.take();
    auto s = nms::decode_character_sets(bytes);
    REQUIRE(s);
    CHECK(s->selected_set == 7);
    REQUIRE(s->sets.size() == 2);
    CHECK(s->sets[1].name == "Alts");
    REQUIRE(s->characters.size() == 1);
    CHECK(s->characters[0].classes == 0x8001);
    CHECK(s->characters[0].sets == std::vector<std::uint32_t>{7});
}

TEST_CASE("NMS cooldowns: FUN_100ecf40 and the 0x1339 slots") {
    nms::Cooldowns c;
    CHECK(c.longest(0) == 10.0f); // the DLL's data
    CHECK(c.longest(1) == 6.0f);
    c.on_skill_timers({12, 0, 4, 9, 0, 0x5}, 1000);
    CHECK(c.autoskill_mask() == 0x5);
    CHECK(c.remaining(0, 1000) == 12.0f);
    CHECK(c.longest(0) == 12.0f); // raised
    CHECK(c.remaining(0, 3500) == doctest::Approx(9.5f));
    CHECK(c.remaining(2, 1000) == 4.0f); // kick
    CHECK(c.remaining(7, 1000) == 4.0f); // flying kick: kick
    CHECK(c.remaining(5, 1000) == 9.0f); // dragon punch: strike
    CHECK(c.remaining(1, 1000) == 0.0f); // 0 clears
    CHECK(c.remaining(2, 9000) == 0.0f); // run out
    c.set(4, 4.0f, 2000);                // OP_Taunt
    CHECK(c.remaining(4, 3000) == 3.0f);
    CHECK(c.longest(4) == 4.0f);
}

TEST_CASE("NMS: the discipline bands and the detour's refusal line") {
    CHECK(nms::discipline_band(4514) == 23);
    CHECK(nms::discipline_band(4499) == 21);
    CHECK(nms::discipline_band(6170) == 329);
    CHECK_FALSE(nms::discipline_band(1));
    CHECK(nms::discipline_not_ready_line("Mighty Strike", 42) == "Mighty Strike will be ready in 42 seconds.");
}

TEST_CASE("NMS tracking rule: Ranger, Druid or Bard in the mask, then the skill") {
    using nms::tracking_allowed;
    CHECK(tracking_allowed(8, 0, 10));          // no mask (other servers): the skill decides
    CHECK_FALSE(tracking_allowed(8, 0, 0));
    CHECK_FALSE(tracking_allowed(8, 0x2, 10));  // Cleric only
    CHECK(tracking_allowed(8, 0x8, 10));        // Ranger (bit 3)
    CHECK_FALSE(tracking_allowed(8, 0x8, 0));
}

TEST_CASE("NMS OP_PetList is u32 count, {u32 spawn id, u32 class}") {
    std::vector<std::byte> raw;
    auto append_u32 = [](std::vector<std::byte>& b, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::byte>(v >> (8 * i)));
    };
    append_u32(raw, 2);
    append_u32(raw, 301);
    append_u32(raw, 11);
    append_u32(raw, 302);
    append_u32(raw, 13);
    auto d = nms::decode_pet_list(raw);
    REQUIRE(d);
    REQUIRE(d->size() == 2);
    CHECK((*d)[0].spawn_id == 301);
    CHECK((*d)[0].class_id == 11);
    CHECK((*d)[1].spawn_id == 302);
    CHECK(nms::encode_pet_list(*d).data == raw);
    // Count larger than the payload: refused.
    std::vector<std::byte> bad;
    append_u32(bad, 1000);
    CHECK_FALSE(nms::decode_pet_list(bad));
    // An empty list (no pets) is valid.
    std::vector<std::byte> empty;
    append_u32(empty, 0);
    auto e = nms::decode_pet_list(empty);
    REQUIRE(e);
    CHECK(e->empty());
}

TEST_CASE("NMS scribe click rules (notes §3.4)") {
    CHECK(nms::scribe_click(20));
    CHECK(nms::scribe_click(31));
    CHECK_FALSE(nms::scribe_click(21));
    // The first item class in the mask, else the item's first class.
    CHECK(nms::scribe_class((1u << 1) | (1u << 11), 1u << 11) == 12);
    CHECK(nms::scribe_class((1u << 1) | (1u << 11), 1u << 0) == 2);
    CHECK(nms::scribe_class(0, 0xffff) == 0);
    CHECK(nms::scribe_is_tome(31, "Spell: Minor Healing"));
    CHECK(nms::scribe_is_tome(20, "Tome of Kick"));
    CHECK(nms::scribe_is_tome(20, "Skill: Bash"));
    CHECK(nms::scribe_is_tome(20, "Discipline of Rumblecrush"));
    CHECK_FALSE(nms::scribe_is_tome(20, "Spell: Minor Healing"));
    CHECK_FALSE(nms::scribe_is_tome(20, "SKILL: upper case"));
}

TEST_CASE("NMS #petcmd lines and the swarm pets' setting lines") {
    CHECK(nms::petcmd_line("attack", "", "all") == "#petcmd attack all");
    CHECK(nms::petcmd_line("hold", "on", "swarm") == "#petcmd hold on swarm");
    CHECK(nms::petcmd_line("spellhold", "off", "nec") == "#petcmd spellhold off nec");
    CHECK_FALSE(nms::petcmd_line("attack", "on", "all"));  // no state on a plain command
    CHECK_FALSE(nms::petcmd_line("taunt", "maybe", "all"));
    CHECK_FALSE(nms::petcmd_line("taunt", "", "swarm"));   // swarm_unsupported_commands
    CHECK_FALSE(nms::petcmd_line("health", "", "swarm"));
    CHECK_FALSE(nms::petcmd_line("attack", "", "mag; #gm on"));
    CHECK_FALSE(nms::petcmd_line("", "", "all"));
    CHECK(nms::pet_class_word(13) == "mag");
    CHECK(nms::pet_class_word(15) == "bst");
    CHECK(nms::pet_class_word(0).empty());
    CHECK(nms::pet_class_word(17).empty());
    CHECK(std::string(nms::pet_class_name(5)) == "Shadow Knight");
    // Every switch takes on/off, and swarm pets take the server's ten.
    int swarm = 0;
    for (const auto& v : nms::pet_verbs()) swarm += v.swarm ? 1 : 0;
    CHECK(swarm == 10);

    CHECK(nms::swarm_setting("Swarm pet focus setting: ON") == std::pair<std::string, bool>{"focus", true});
    CHECK(nms::swarm_setting("Swarm pet assist setting: OFF") == std::pair<std::string, bool>{"assist", false});
    CHECK_FALSE(nms::swarm_setting("Swarm pet focus setting: on"));
    CHECK_FALSE(nms::swarm_setting("Swarm pet  setting: ON"));
    CHECK_FALSE(nms::swarm_setting("Your pet focus setting: ON"));
}
