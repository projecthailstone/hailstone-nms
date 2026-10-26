// The NMS module's connection and class hooks (NMS/dinput8.dll; rof2decomp notes/nms-dinput8.md,
// plugins/nms/nms_rules.h). The patch's incoming opcodes, its outgoing OP_CAuth and 0x140f report, the class mask
// and the hooked GetSpellLevelNeeded, and its per-frame Heartbeat. Everything here runs only once the connection
// detects an NMS server (GameConnection::nms_mode()).

#include "plugins/nms/nms_extension.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <format>

#include "core/config.h"
#include "core/log.h"
#include "game/eq_names.h"
#include "game/slash_commands.h"
#include "game/spells.h"
#include "net/game_connection.h"
#include "net/opcodes.h"
#include "net/social_packets.h"

namespace nmsc::nms {

using net::ConnectionState;
using net::GameConnection;
using net::Op;

namespace {

void read_auth_key(NmsOptions& o, const std::string& key) {
    o.auth_key = static_cast<std::uint32_t>(std::strtoul(key.c_str(), nullptr, 10));
}

// nms.hardware_id = fixed (default: the NMS patch's VM values in the login CRCs) | none (the stock CRCs).
// Real hardware identifiers are never read or sent (plugins/nms/nms_packets.h).
void read_hardware_id(NmsOptions& o, const std::string& hw) {
    if (auto mode = parse_hardware_id_mode(hw)) o.hardware_id = *mode;
    else log::warn("nms.hardware_id: unknown value '{}' (fixed | none); using fixed", hw);
}

} // namespace

NmsOptions read_options(const Config& config) {
    NmsOptions o;
    if (auto key = config.get("nms.auth_key")) read_auth_key(o, *key);
    if (auto hw = config.get("nms.hardware_id")) read_hardware_id(o, *hw);
    return o;
}

NmsOptions read_options(const Config& config, const ServerProfile& profile) {
    NmsOptions o = read_options(config);
    if (!profile.nms_auth_key.empty()) read_auth_key(o, profile.nms_auth_key);
    if (!profile.nms_hardware_id.empty()) read_hardware_id(o, profile.nms_hardware_id);
    return o;
}

void set_server_module(net::ConnectionOptions& options, ServerModule module, const NmsOptions& nms) {
    options.extension.reset();
    options.force_extension = false;
    if (module == ServerModule::None) return;
    options.extension = std::make_shared<NmsExtension>(nms);
    options.force_extension = module == ServerModule::Nms;
}

// ---------------------------------------------------------------- classes

std::uint32_t NmsExtension::class_mask(const GameConnection& c) const {
    // The patch's class mask (g_cauth_bitmask): OP_ServerAuthStats key 1, until it arrives the character set's mask.
    if (auto it = c.server_stats().find(1); it != c.server_stats().end() && it->second != 0)
        return static_cast<std::uint32_t>(it->second);
    return character_class_mask(c, c.character_name());
}

std::uint32_t NmsExtension::character_class_mask(const GameConnection& c, const std::string& name) const {
    // Multiclass bit masks from NMS's character sets (OP_SendCharacterSets), by character name.
    if (const auto& sets = c.character_sets())
        for (const auto& ch : sets->characters)
            if (ch.name == name) return ch.classes;
    return 0;
}

std::optional<bool> NmsExtension::is_spellcaster(const GameConnection& c) const {
    // The patch's IsSpellcaster detours answer (mask & 0x7ebe) != 0 for the local character.
    if (std::uint32_t mask = class_mask(c)) return nms::is_spellcaster(mask);
    return std::nullopt;
}

std::optional<std::uint8_t> NmsExtension::spell_level_needed(const GameConnection& c, const game::SpellData& spell,
                                                             std::uint32_t class_id) const {
    const std::uint32_t mask = class_mask(c);
    if (mask == 0) return std::nullopt;
    // The trampoline: EQ_Spell::GetSpellLevelNeeded reads the class's level byte (classes outside 1..16: the
    // warrior byte, as the memorise port reads it).
    auto orig = [&spell](std::uint32_t k) -> std::uint8_t {
        return spell.class_level[(k >= 1 && k <= 16 ? k : 1) - 1];
    };
    // g_scribeInProgress, or a type-20 scroll of this spell on the cursor.
    const auto& spells = c.spell_state();
    const bool scribe = scribe_in_progress_ || (spells.scribing && spells.scribing->spell == spell.id) ||
                        c.cursor_scroll_spell() == spell.id;
    // The detour reads the spawn's class (PlayerClient +0xeb8), which the Heartbeat's cast spoof may have changed.
    return nms::spell_level_needed(orig, class_id, c.visible_class(), mask, c.world().player() != nullptr, scribe);
}

bool NmsExtension::song_blocked(const GameConnection& c, std::uint8_t raw_bard_level) const {
    // The patch's BardCastBard detour: a spell whose Bard level byte (+0x24e) is 255 is never a song.
    return class_mask(c) != 0 && bard_cast_bard_blocked(raw_bard_level);
}

bool NmsExtension::own_begin_cast_line(const GameConnection& c, const game::SpellData& s) const {
    // The server's 12205 skips bard songs (EQEmu IsBardSong: a Bard level below 255); those keep CastByMe.
    if (class_mask(c) == 0 || s.class_level[kBard - 1] != 0xff) return false;
    return own_begin_cast_dropped(c.chat_filters().settings[game::kFilterPcSpells], c.group().active,
                                  c.raid().active);
}

std::optional<bool> NmsExtension::can_use_gem(const GameConnection& c, int gem) const {
    // The patch's CanUseMemorizedSpellSlot detour: gem < 12, from level 1.
    if (class_mask(c) == 0) return std::nullopt;
    return can_use_memorized_spell_slot(gem);
}

std::int32_t NmsExtension::combat_ability_timer(const GameConnection& c, const game::SpellData& s) const {
    if (auto band = discipline_band(s.id)) return *band;
    return ServerExtension::combat_ability_timer(c, s);
}

std::optional<std::uint32_t> NmsExtension::discipline_reuse_timer(const GameConnection&, std::int32_t id) const {
    // The GetDisciplineReuseTimer detour (FUN_10109780): ids >= 20 from its map, 0 (ready) when absent.
    if (!DisciplineTimers::handles(id)) return std::nullopt;
    return disc_timers_.get(id);
}

std::optional<bool> NmsExtension::before_combat_ability(GameConnection& c, const game::SpellData& s) {
    // The doCombatAbility detour (FUN_10109820) for a timer id >= 20: refused while its map timer runs.
    combat_now_ = static_cast<std::uint32_t>(std::time(nullptr));
    const std::int32_t timer = combat_ability_timer(c, s);
    if (!DisciplineTimers::handles(timer) || disc_timers_.ready(timer, combat_now_)) return std::nullopt;
    c.ui_message(discipline_not_ready_line(s.name, disc_timers_.remaining(timer, combat_now_)));
    return false;
}

void NmsExtension::after_combat_ability(GameConnection& c, const game::SpellData& s, bool used) {
    // ... and stamped now + RecastTime / 1000 when the original used it.
    const std::int32_t timer = combat_ability_timer(c, s);
    if (used && DisciplineTimers::handles(timer)) disc_timers_.set(timer, combat_now_ + s.recast_time / 1000);
}

std::optional<std::string> NmsExtension::combat_effect_label(const GameConnection& c) const {
    for (const auto& b : c.spell_state().buffs) {
        if (b.empty()) continue;
        const game::SpellData* s = c.spell_table().get(b.spell);
        if (s && DisciplineTimers::handles(combat_ability_timer(c, *s))) return s->name;
    }
    return std::string("No Effect");
}

std::string NmsExtension::class_desc(const GameConnection& c, std::int32_t id) const {
    auto desc = [](std::int32_t k) { return game::class_name(static_cast<std::uint32_t>(k)); };
    // The character select slots: the masks the DLL took from OP_SendCharInfo's Deity (block +0xf3) by slot.
    const auto& chars = c.characters();
    auto slot_mask = [&](std::int32_t slot) -> std::uint32_t {
        if (slot < 0 || static_cast<std::size_t>(slot) >= chars.size()) return 0;
        return character_class_mask(c, chars[static_cast<std::size_t>(slot)].name);
    };
    auto slot_class = [&](std::int32_t slot) -> std::uint32_t {
        if (slot < 0 || static_cast<std::size_t>(slot) >= chars.size()) return 0;
        return chars[static_cast<std::size_t>(slot)].class_;
    };
    return nms::class_desc(id, desc, slot_mask, slot_class);
}

std::string NmsExtension::guild_member_class(const GameConnection& c, std::uint32_t class_value) const {
    // EQEmu stores NMS multiclass members as 1000 + the class bit mask, which the patch's GetClassDesc detour shows
    // as "War/Clr".
    if (class_value >= 1000) return class_abbrevs(class_value - 1000);
    return ServerExtension::guild_member_class(c, class_value);
}

std::string NmsExtension::window_title(const GameConnection& c) const {
    // The Heartbeat's window title ("EverQuest - Name [War/Clr]") less "EverQuest - ", in the zone.
    if (c.state() != ConnectionState::InZone || c.character_name().empty()) return {};
    return nms::window_title(c.character_name(), class_mask(c));
}

game::CharacterVitals NmsExtension::character_vitals(const GameConnection& c) const {
    // The patch's stat map is GameConnection::server_stats() (keys 1..0x83 as they came, never cleared; the patch
    // clears its map only when the client sends 0x166c, docs/nms-module.md).
    return server_vitals(c.server_stats());
}

std::vector<std::uint8_t> NmsExtension::max_vitals_classes(const GameConnection& c, std::uint8_t class_id) const {
    // NMS multiclass servers take the best of the unlocked classes (zone/client_mods.cpp CalcBaseHP, CalcBaseMana,
    // CalcBaseEndurance).
    const std::uint32_t mask = class_mask(c);
    if (!mask || mask == class_bit(class_id)) return {class_id};
    std::vector<std::uint8_t> out;
    for (std::uint8_t k = 1; k <= 16; ++k)
        if (has_class(mask, k)) out.push_back(k);
    return out;
}

const game::SkillRules* NmsExtension::skill_rules() const {
    // The HasSkill / IsAvailable detours (§3.8) and the tracking window's rule.
    static const game::SkillRules kRules{&nms::skill_gate, &nms::skill_spoof_class, &nms::tracking_allowed};
    return &kRules;
}

// ---------------------------------------------------------------- the connection

void NmsExtension::request_waypoint(GameConnection& c, std::int32_t id, bool expedition) {
    if (c.state() != ConnectionState::InZone || !waypoints_) return;
    WaypointRequest r;
    r.id = id;
    r.expedition = expedition;
    r.group = waypoints_->group_selected;
    r.autoconfirm = waypoints_->autoconfirm_selected;
    c.send_zone_packet(encode(r));
}

void NmsExtension::send_waypoint_request(GameConnection& c, const WaypointRequest& r) {
    if (c.state() != ConnectionState::InZone) return;
    c.send_zone_packet(encode(r));
}

namespace {

// MacroQuest's chat colours as the add-on's WriteChatColor gives them (EQData.h CONCOLOR_*).
constexpr std::uint32_t kConGreen = 0x02, kConGrey = 0x06, kConYellow = 0x0f;

// multi_pet.cpp GetClassName: the summoning class OP_PetList carries.
const char* pet_class_name(std::uint32_t id) {
    static constexpr const char* kNames[] = {"Warrior", "Cleric", "Paladin", "Ranger", "Shadow Knight", "Druid", "Monk",
                                             "Bard", "Rogue", "Shaman", "Necromancer", "Wizard", "Magician",
                                             "Enchanter", "Beastlord", "Berserker"};
    return id >= 1 && id <= 16 ? kNames[id - 1] : "Unknown";
}

} // namespace

void NmsExtension::pets_command(GameConnection& c) {
    // MultiPet::ListPets: the spawns' own names ("(spawning)" before the spawn is here), the summoning class and the
    // pet in the pet window (the local player's PetID).
    const auto& pets = c.pets();
    if (!pets.have_list || pets.list.empty()) {
        c.ui_message("You have no pets.", 0, kConYellow);
        return;
    }
    c.ui_message(std::format("You have {} pet(s):", pets.list.size()), 0, kConYellow);
    for (std::size_t i = 0; i < pets.list.size(); ++i) {
        const auto& pet = pets.list[i];
        auto it = c.world().spawns.find(pet.spawn_id);
        std::string name = it == c.world().spawns.end() ? "(spawning)" : it->second.name;
        std::string line = std::format("  {}: {}", i + 1, name);
        if (pet.class_id > 0) line += std::format(" [{}]", pet_class_name(pet.class_id));
        if (pet.spawn_id == pets.focused) line += " (pet window)";
        c.ui_message(std::move(line), 0, kConGreen);
    }
}

void NmsExtension::petcycle_command(GameConnection& c) {
    // MultiPet::CyclePet: the pet after the window's in the list (the first when the window's is not listed) goes
    // into the pet window; only the local PetID changes, the server is not told.
    const auto& pets = c.pets();
    if (pets.list.empty()) return;
    std::size_t i = 0;
    while (i < pets.list.size() && pets.list[i].spawn_id != pets.focused) ++i;
    c.set_window_pet(pets.list[i >= pets.list.size() ? 0 : (i + 1) % pets.list.size()].spawn_id);
}

void NmsExtension::petdebug_command(GameConnection& c) {
    // MultiPet::DebugSpawns (the spawn pointers are this client's spawn table here).
    const auto& pets = c.pets();
    c.ui_message(std::format("MultiPet Debug: localID={}  uiPetID={}  havePetList={}  trackedPets={}", c.world().player_id,
                             pets.focused, pets.have_list ? 1 : 0, pets.list.size()),
                 0, kConYellow);
    for (std::size_t i = 0; i < pets.list.size(); ++i) {
        const auto& pet = pets.list[i];
        auto it = c.world().spawns.find(pet.spawn_id);
        c.ui_message(std::format("  Pet[{}]: spawnID={} name='{}' classID={} spawned={}", i, pet.spawn_id,
                                 it == c.world().spawns.end() ? std::string() : it->second.name, pet.class_id,
                                 it == c.world().spawns.end() ? "no" : "yes"),
                     0, kConGreen);
    }
    c.ui_message(std::format("  Total spawns iterated: {}", c.world().spawns.size()), 0, kConGrey);
}

void NmsExtension::waypoint_command(GameConnection& c, const std::string& args) {
    // No argument opens the waypoint window, "list" lists the waypoints in chat, an id travels there.
    if (!waypoints_) {
        c.ui_message("No waypoint list from the server yet: click the Magic Map or a waypoint.");
        return;
    }
    if (args.empty()) {
        waypoint_window_.show = true;
        ++waypoint_serial_; // the Lua WaypointsWnd shows again too (plugins/nms/nms_lua.cpp)
        return;
    }
    if (args == "list") {
        for (const auto& e : waypoints_->entries)
            c.ui_message(std::to_string(e.id) + ": " + e.name + (e.enabled ? "" : " (locked)"));
        return;
    }
    request_waypoint(c, std::atoi(args.c_str()));
}

void NmsExtension::send_loot(GameConnection& c, const LootSend& a) {
    // The patch's Send: only in the game, on the zone connection.
    if (c.state() != ConnectionState::InZone) return;
    c.send_zone_packet(net::encode_personal_loot_action(a.action, a.pending_id, a.item_id, a.value));
}

void NmsExtension::ensure_loot_rules(GameConnection& c) {
    // LoadRulesIfNeeded: the character's itemrules.ini (the patch's <name>_itemrules.ini), read when the character
    // changes. (The patch also rereads while it has no rules, every pulse; "/loot rules" rereads a hand edit.)
    const CharacterFiles files = c.own_files();
    const std::filesystem::path path = files.empty() ? std::filesystem::path() : files.dir / "itemrules.ini";
    if (path != loot_.rules().path()) loot_.rules().load(path);
}

void NmsExtension::loot_command(GameConnection& c, const std::string& args) {
    ensure_loot_rules(c);
    LootCommandResult r = nms::loot_command(loot_, args, GameConnection::now_ms());
    for (const auto& line : r.lines) c.ui_message(line);
    for (const auto& a : r.sends) send_loot(c, a);
    if (r.reset_window) ++loot_reset_serial_;
}

void NmsExtension::autoskill_command(GameConnection& c, const std::string& args) {
    // FUN_1016c310: no argument toggles the console and says so; anything else goes to the server's #autoskill.
    std::size_t i = 0;
    while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
    if (i == args.size()) {
        autoplay_console = !autoplay_console.value_or(false);
        c.ui_message(std::string("[NMS] Multiclass Autoplay Console ") + (*autoplay_console ? "Enabled." : "Disabled."));
        return;
    }
    c.send_command("#autoskill " + args.substr(i));
}

void NmsExtension::send_cauth(GameConnection& c) {
    // FUN_100ed9f0: once per zone, after the class mask and the spawn id are known (EnterZone re-arms it).
    std::uint32_t mask = class_mask(c);
    if (mask == 0 && c.profile() && c.profile()->class_ > 0) mask = 1u << (c.profile()->class_ - 1);
    c.send_zone_packet(encode_cauth(mask, c.world().player_id, options_.auth_key));
}

void NmsExtension::zone_entered(GameConnection& c) {
    if (options_.auth_key == 0 || c.world().player_id == 0) return;
    // NMS OP_CAuth: the first 8 bytes, XORed with the server key, must read
    // as classes bit mask × own spawn id (zone/client_packet.cpp Handle_OP_CAuth).
    send_cauth(c);
    auth_sent_ = true;
    auth_time_ = std::chrono::steady_clock::now();
}

bool NmsExtension::retry_zone_entry(GameConnection&) {
    // Answer the NMS client check once more if the server did not echo it.
    if (auth_sent_ && options_.auth_key && std::chrono::steady_clock::now() - auth_time_ > std::chrono::seconds(8)) {
        auth_sent_ = false;
        return true;
    }
    return false;
}

void NmsExtension::zone_server_changed(GameConnection&) {
    report_sent_ = warned_ = auth_sent_ = false; // EnterZone re-arms the patch's sends
    zone_in_ms_ = 0;
    cast_class_ = 0;
    waypoints_.reset();
    waypoint_window_.show = false; // WaypointsWnd::OnSetGameState hides it outside the game
    loot_.reset(false);            // the Personal Loot window hides and says Hello again in the new zone
}

void NmsExtension::outgoing_checksum(GameConnection& c, net::AppPacket& p) {
    // The NMS patch's CRC rewrite (plugins/nms/nms_packets.h), on an NMS world only (OP_SendCharacterSets came first).
    if (c.character_sets() && apply_hardware_id(p, options_.hardware_id))
        log::info("NMS: {} carries the fixed hardware id", net::op_name(net::from_wire(p.opcode)));
}

void NmsExtension::server_stats_changed(GameConnection& c) {
    // FUN_10153300 writes keys 2/5/3/6/4/7 into the profile's current / max HP, mana and endurance.
    const auto& stats = c.server_stats();
    auto stat = [&](std::uint32_t k) -> const std::int64_t* {
        auto it = stats.find(k);
        return it == stats.end() ? nullptr : &it->second;
    };
    auto clamp = [](std::int64_t v) { return std::clamp<std::int64_t>(v, 0, 0x7ffffffe); };
    net::PlayerStatus& ps = c.player_status_mut();
    if (const auto* v = stat(2)) ps.hp = clamp(*v);
    if (const auto* v = stat(5)) ps.max_hp = clamp(*v);
    if (const auto* v = stat(3)) ps.mana = static_cast<std::uint32_t>(clamp(*v));
    if (const auto* v = stat(6)) ps.max_mana = static_cast<std::uint32_t>(clamp(*v));
    if (const auto* v = stat(4)) ps.endurance = static_cast<std::uint32_t>(clamp(*v));
    if (const auto* v = stat(7)) ps.max_endurance = static_cast<std::uint32_t>(clamp(*v));
}

bool NmsExtension::command(GameConnection& c, const std::string& word, const std::string& args) {
    if (word == "/nmsloot") { // the shipped patch's name for its loot window's command (plugins/nms/nms_loot.h)
        loot_command(c, args);
        return true;
    }
    if (word == "/fov" || word == "/camera") { // the patch's CmdFov: the max zoom distance (nms_ui.cpp)
        c.request_window_toggle("fov " + args);
        return true;
    }
    if (word == "/autoskill") { // the patch's console (FUN_1016c310)
        autoskill_command(c, args);
        return true;
    }
    if (word == "/waypoint" || word == "/waypoints") { // the patch's waypoint window, as text
        waypoint_command(c, args);
        return true;
    }
    if (word == "/pets") { // MultiPet's commands (multi_pet.cpp)
        pets_command(c);
        return true;
    }
    if (word == "/petcycle") {
        petcycle_command(c);
        return true;
    }
    if (word == "/petdebug") {
        petdebug_command(c);
        return true;
    }
    return false;
}

bool NmsExtension::handler_command(GameConnection& c, std::string_view handler, const std::string& args) {
    if (handler == "__do_loot") {
        // The patch's AddCommand("/loot", LootCommand) takes /loot over: its Personal Loot window.
        loot_command(c, args);
        return true;
    }
    if (handler != "cmd_alternateadv") return false;
    // The NMS patch's detour (cmd_alternateadv, Hooks.cpp): "/alt toggle <id>" becomes "/say #alttoggle <id>"
    // (an NMS server command); other forms go on to the original.
    std::string_view rest = args;
    std::string first = game::next_cmd_arg(rest);
    std::string second = game::next_cmd_arg(rest);
    for (char& ch : first) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (first != "toggle") return false;
    bool numeric = !second.empty() && second.size() <= 10 &&
                   std::all_of(second.begin(), second.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    if (!numeric) c.ui_message("Usage: /alt toggle <AA Id>");
    else c.send_command("/say #alttoggle " + second);
    return true;
}

bool NmsExtension::hide_chat_line(GameConnection& c, const std::string& text) {
    // The patch's dsp_chat detour (FUN_10119d80): "Your song ends." for a mask without Bard, the vault's lines and
    // the echoes of the vault's and console's own commands ("You say, '#autoskill ...", nms_rules.h hide_patch_line).
    if (text == "Your song ends." && hide_song_ends(class_mask(c))) return true;
    vault_.on_chat(text); // the vault's VAULTDATA lines (FUN_10199a20), hidden below
    return hide_patch_line(text);
}

std::optional<std::vector<std::string>> NmsExtension::who_lines(const GameConnection&,
                                                                const net::WhoResponse& r) const {
    // The NMS patch replaces the whole reply with its own lines (FUN_10198eb0, plugins/nms/nms_rules.h).
    return format_who(r);
}

// The HandleWorldMessage detour's opcodes (0x140f, the server's acknowledgement, is one of the core's detection
// signals: GameConnection::handle_world / handle_zone). True = the patch swallows the packet (the original never
// sees it).
bool NmsExtension::packet(GameConnection& c, const net::AppPacket& p) {
    const std::int64_t now = GameConnection::now_ms();
    switch (net::from_wire(p.opcode)) {
    case Op::PersonalLootList:
        // The Personal Loot window's OnList (plugins/nms/nms_loot.h): the rows a rule covers are answered now.
        // Passed on (the core keeps the list too).
        if (auto l = net::decode_personal_loot_list(p)) {
            ensure_loot_rules(c);
            for (const LootSend& a : loot_.on_list(*l, now)) send_loot(c, a);
        }
        return false;
    case Op::PersonalLootFilters:
        // The server's own filters, for clients without the patch: the patch accepts and ignores them.
        return true;
    case Op::PersonalLootSettings:
        if (auto s = decode_loot_settings(p)) loot_.on_settings(*s);
        return true;
    case Op::PersonalLootHistory:
        if (auto h = decode_loot_history(p)) loot_.on_history(std::move(*h), now);
        return true;
    case Op::SkillTimers:
        // {backstab, bash, kick, strike, frenzy, autoskill mask} -> the patch's cooldown table (0x1035d634). The
        // same handler also runs FUN_100eca90 on this buffer (byte 0: 0 merchant / 1 bank for the spawn id at +1);
        // why is open (notes/nms-dinput8.md §5) and EQEmu's buffer starts with the backstab timer, so the client
        // does not open windows from it. Passed on (the original has no handler for it).
        if (auto t = decode_skill_timers(p)) {
            skill_timers_ = *t;
            cooldowns_.on_skill_timers(*t, static_cast<std::uint32_t>(GameConnection::now_ms()));
        }
        return true;
    case Op::Taunt:
        // The patch's cooldown slot 4 (Taunt) := 4 s, then passed on.
        cooldowns_.set(4, 4.0f, static_cast<std::uint32_t>(GameConnection::now_ms()));
        return false;
    case Op::WaypointList:
        // The waypoint window's list (plugins/nms/nms_waypoints.h); the patch shows its window on every one.
        // Swallowed.
        waypoints_ = decode_waypoint_list(p);
        if (waypoints_) {
            waypoint_window_.show = true;
            ++waypoint_serial_;
        }
        return true;
    case Op::Weblink:
        // The patch opens the URL in its WebView2 or the system browser per /browser (default off: ignored).
        // This client never opens a browser itself: it shows the URL in chat. Swallowed.
        if (auto url = decode_weblink(p)) c.ui_message("Web link: " + *url);
        return true;
    case Op::DisciplineTimer:
        // {u32 id, u32 seconds left, u32}: the stock SetDisciplineReuseTimer keeps ids 0..19; the patch's detour
        // keeps ids >= 20 in its map (ready = now + seconds). Passed on.
        if (p.data.size() >= 8) {
            const std::uint32_t id = std::to_integer<std::uint32_t>(p.data[0]) |
                                     std::to_integer<std::uint32_t>(p.data[1]) << 8 |
                                     std::to_integer<std::uint32_t>(p.data[2]) << 16 |
                                     std::to_integer<std::uint32_t>(p.data[3]) << 24;
            const std::uint32_t secs = std::to_integer<std::uint32_t>(p.data[4]) |
                                       std::to_integer<std::uint32_t>(p.data[5]) << 8 |
                                       std::to_integer<std::uint32_t>(p.data[6]) << 16 |
                                       std::to_integer<std::uint32_t>(p.data[7]) << 24;
            if (DisciplineTimers::handles(static_cast<std::int32_t>(id)))
                disc_timers_.set(static_cast<std::int32_t>(id), static_cast<std::uint32_t>(std::time(nullptr)) + secs);
        }
        return false;
    case Op::MulticlassCharSelect:
        // {u32 count; u32 mask[count]} by character select slot; swallowed. The NMS world does not send it (the
        // masks come in OP_SendCharacterSets and OP_SendCharInfo's Deity).
        return true;
    case Op::CAuth:
        auth_sent_ = false; // echoed: the server accepted us
        log::info("NMS client check accepted");
        return true;
    default:
        return false;
    }
}

// The patch's per-frame Heartbeat (FUN_1016b670, §3.3), the parts with client state: the cast spoof, the global
// recovery removal, the OP_CAuth / 0x140f sends and the "non multiclass server" warning.
bool NmsExtension::item_right_click(GameConnection& c, const net::ItemSlot& slot) {
    if (c.state() != ConnectionState::InZone || !c.world().player()) return false;
    const net::Item* item = c.inventory().at(slot);
    if (!item || !scribe_click(item->item_type)) return false;
    const std::uint8_t target = scribe_class(item->classes, class_mask(c));
    // A tome learns at once, the book closed; with the book open a scroll scribes at once too.
    if (scribe_is_tome(item->item_type, item->name) || c.spell_book_open()) {
        run_scribe(c, slot, target);
        return true;
    }
    // Else: /book now, the sit on the next frame, the click again 100 ms later.
    c.send_command("/book");
    show_book_pending_ = true;
    scribe_ = PendingScribe{slot, target, GameConnection::now_ms() + kScribeDelayMs};
    return true;
}

void NmsExtension::run_scribe(GameConnection& c, const net::ItemSlot& slot, std::uint8_t target_class) {
    // NMS_BeginScribeSpoof: class = the target class (when there is one) and level 100 on the spawn and profile,
    // g_scribeInProgress; then the original HandleRButtonUp, then everything back. Of the spoof only the class
    // reaches the original (its casting check's BardCastBard); the item type 31 the tome path also writes is not
    // read there (rof2decomp export CInvSlot.c and the disassembly @0x697250).
    scribe_in_progress_ = true;
    c.right_click_item_original(slot, {}, target_class);
    scribe_in_progress_ = false;
}

void NmsExtension::heartbeat(GameConnection& c) {
    if (c.state() != ConnectionState::InZone) {
        cast_class_ = 0;
        show_book_pending_ = book_opened_by_scribe_ = false;
        scribe_.reset();
        return;
    }
    // The Personal Loot window's OnPulse: its rules for this character, and the Hello once per zone.
    ensure_loot_rules(c);
    if (auto hello = loot_.pulse(c.world().player() != nullptr, GameConnection::now_ms())) send_loot(c, *hello);
    const std::uint32_t mask = class_mask(c);
    const auto& spells = c.spell_state();
    // The cast spoof: while a spell whose Bard level is 255 is being cast, the first mask class that can cast it.
    cast_class_ = 0;
    if (mask && spells.casting.casting()) {
        if (const auto* s = c.spell_table().get(spells.casting.spell); s && s->class_level[kBard - 1] == 0xff) {
            auto orig = [s](std::uint32_t k) { return s->class_level[k - 1]; };
            cast_class_ = static_cast<std::uint8_t>(cast_spoof_class(orig, mask));
        }
    }
    // SpellCooldownETA (+0x3cc) zeroed whenever no cast is running: no global spell recovery.
    if (!spells.casting.casting()) c.clear_spell_recovery();
    // The scribe path (§3.4, MQ2Pulse): the book opened for a scroll sits the player down on the next frame,
    // standing up again closes it, and the click is replayed once its 100 ms are up.
    const auto& me = c.player_status();
    if (show_book_pending_) {
        show_book_pending_ = false;
        if (me.stand_state != 110) c.send_command("/sit");
        book_opened_by_scribe_ = true;
    }
    if (book_opened_by_scribe_ && me.stand_state != 110) {
        book_opened_by_scribe_ = false;
        c.close_spell_book();
    }
    if (scribe_ && GameConnection::now_ms() >= scribe_->due_ms) {
        const PendingScribe p = *scribe_;
        scribe_.reset();
        if (c.world().player()) run_scribe(c, p.slot, p.target_class);
    }
    // The 0x140f report, once after the mask arrived (key 1).
    if (!report_sent_ && c.server_stats().count(1) && c.world().player_id != 0) {
        c.send_zone_packet(encode_client_report(options_.report_flags));
        report_sent_ = true;
    }
    // "WARNING! -- You are on a NON Multiclass Server ...": the mask still 0 twenty seconds after the spawn id,
    // and no 0x140f from the server.
    const std::int64_t now = GameConnection::now_ms();
    if (zone_in_ms_ == 0) zone_in_ms_ = now;
    if (!warned_ && !c.nms_server_ack() && mask == 0 && now - zone_in_ms_ > 20000) {
        warned_ = true;
        c.ui_message("WARNING! -- You are on a NON Multiclass Server with the Multiclass Client. Please change servers "
                     "or client.");
    }
}

} // namespace nmsc::nms
