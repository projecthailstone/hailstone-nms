// The NMS plugin's packets, hooks, commands and per-frame work (NMS/dinput8.dll; rof2decomp notes/nms-dinput8.md,
// nms_rules.h): the patch's incoming opcodes, its outgoing OP_CAuth and 0x140f report, the class mask and the hooked
// GetSpellLevelNeeded, the scribe path and its per-frame Heartbeat. Nothing here runs before the plugin has seen an
// NMS server (Nms::detected) but the OP_PetList pet list, which the core used to keep on any server.

#include "nms_plugin.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <system_error>

#include "nms_ini.h"

NMSC_PLUGIN

namespace nms {

namespace fs = std::filesystem;

void register_ui_functions(); // nms_ui.cpp

namespace {

Nms g_nms;

// MacroQuest's chat colours as the add-on's WriteChatColor gives them (EQData.h CONCOLOR_*).
constexpr std::uint32_t kConGreen = 0x02, kConGrey = 0x06, kConYellow = 0x0f;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A folder the host writes into `fn`'s buffer; empty when it has none.
template <typename F> fs::path host_dir(F fn) {
    if (!nmsc || !fn) return {};
    char buf[1024];
    const int n = fn(nmsc->host, buf, sizeof buf);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof buf) return {};
    return fs::path(std::string(buf, static_cast<std::size_t>(n)));
}

// The client's command words (game/slash_commands' next_cmd_arg): a quoted argument keeps its spaces.
std::string next_arg(std::string_view& rest) {
    std::size_t i = 0;
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
    std::string word;
    if (i < rest.size() && (rest[i] == '\'' || rest[i] == '"')) {
        char quote = rest[i++];
        while (i < rest.size() && rest[i] != quote) word.push_back(rest[i++]);
        if (i < rest.size()) ++i;
    } else {
        while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t' && rest[i] != '\x12') word.push_back(rest[i++]);
    }
    rest.remove_prefix(i);
    return word;
}

// multi_pet.cpp GetClassName: the summoning class OP_PetList carries.
const char* pet_class_name(std::uint32_t id) {
    static constexpr const char* kNames[] = {"Warrior", "Cleric", "Paladin", "Ranger", "Shadow Knight", "Druid", "Monk",
                                             "Bard", "Rogue", "Shaman", "Necromancer", "Wizard", "Magician",
                                             "Enchanter", "Beastlord", "Berserker"};
    return id >= 1 && id <= 16 ? kNames[id - 1] : "Unknown";
}

// The commands the patch and its add-ons add, from the moment the plugin sees an NMS server.
constexpr const char* kCommands[] = {"/nmsloot", "/fov",    "/camera",    "/autoskill", "/waypoint",
                                     "/waypoints", "/pets", "/petcycle", "/petdebug"};

void on_command(void* user, const char* args) { g_nms.command(static_cast<const char*>(user), args ? args : ""); }

int get_xtarget_hide_pets(void*) { return g_nms.options.xtarget_hide_pets ? 1 : 0; }
void set_xtarget_hide_pets(void*, int value) {
    g_nms.options.xtarget_hide_pets = value != 0;
    g_nms.save_options();
}

fs::path options_file() { return host_dir(nmsc ? nmsc->config_dir : nullptr) / "nms.ini"; }

} // namespace

Nms& plugin() { return g_nms; }

// ---------------------------------------------------------------- the host in C++

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

NmscPlayer player() {
    NmscPlayer p{};
    p.size = sizeof p;
    if (!nmsc || !nmsc->get_player(nmsc->host, &p)) p = NmscPlayer{};
    return p;
}

bool player_spawned(const NmscPlayer& p) { return p.spawn_id != 0 && spawn(p.spawn_id).has_value(); }
bool in_zone(const NmscPlayer& p) { return p.in_zone != 0; }

std::optional<NmscSpell> spell(std::uint32_t id) {
    NmscSpell s{};
    s.size = sizeof s;
    if (!nmsc || !nmsc->get_spell(nmsc->host, id, &s)) return std::nullopt;
    return s;
}

std::optional<NmscSpawn> spawn(std::uint32_t id) {
    NmscSpawn s{};
    s.size = sizeof s;
    if (!nmsc || !nmsc->get_spawn(nmsc->host, id, &s)) return std::nullopt;
    return s;
}

std::optional<NmscItem> item(const NmscItemSlot& slot) {
    NmscItem it{};
    it.size = sizeof it;
    if (!nmsc || !nmsc->get_item(nmsc->host, &slot, &it)) return std::nullopt;
    return it;
}

void chat(const std::string& text, std::uint32_t color) {
    if (nmsc) nmsc->write_chat_color(nmsc->host, text.c_str(), color);
}

void command_line(const std::string& line) {
    if (nmsc) nmsc->do_command(nmsc->host, line.c_str());
}

void send(const Packet& p, int stream) {
    if (nmsc) nmsc->send_packet(nmsc->host, stream, p.opcode, p.data.data(), p.data.size());
}

std::string zone_name(std::uint32_t zone_id) {
    char buf[64];
    if (!nmsc || nmsc->zone_name(nmsc->host, zone_id, buf, sizeof buf) <= 0) return {};
    return buf;
}

void log_info(const std::string& text) {
    if (nmsc) nmsc->log(nmsc->host, 1, text.c_str());
}

// ---------------------------------------------------------------- options

void Nms::save_options() {
    const fs::path file = options_file();
    if (file.parent_path().empty()) return;
    Ini ini;
    ini.load(file);
    ini.set("NMS", "xtarget_hide_pets", options.xtarget_hide_pets ? "1" : "0");
    ini.save(file);
}

// ---------------------------------------------------------------- the plugin's life

void Nms::initialize() {
    // Inactive until it sees an NMS server: on any other server the stock client throughout.
    nmsc->set_active(nmsc->host, 0);
    Ini global, profile, server;
    global.load(options_file());
    // servers/<profile>/plugin-config/nms/: the server's own nms.ini, and its server.ini two folders up.
    const fs::path server_dir = host_dir(nmsc->server_config_dir);
    const bool has_profile = !server_dir.empty() && profile.load(server_dir.parent_path().parent_path() / "server.ini");
    const bool has_server = !server_dir.empty() && server.load(server_dir / "nms.ini");
    std::vector<std::string> warnings;
    options = read_options({&global, has_profile ? &profile : nullptr, has_server ? &server : nullptr}, &warnings);
    for (const auto& w : warnings) nmsc->log(nmsc->host, 2, ("nms.ini " + w).c_str());
    register_ui_functions();
}

void Nms::shutdown() {}

void Nms::activate() {
    if (detected_) return;
    detected_ = true;
    nmsc->set_active(nmsc->host, 1);
    // The patch's switches: 8-digit item ids in links (SAY_LINK_BODY_SIZE 77); its class test NOPed (0x45ea0f,
    // 75 10 -> 90 90: moving breaks Hide unless sneaking, any class); AAs from level 1 and Train All. CanUseItem's
    // detour follows the class mask (update_flags).
    nmsc->set_flag(nmsc->host, NMSC_FLAG_WIDE_ITEM_LINKS | NMSC_FLAG_HIDE_ANY_CLASS | NMSC_FLAG_AA_ANY_LEVEL |
                                   NMSC_FLAG_AA_TRAIN_ALL,
                   1);
    update_flags();
    for (const char* c : kCommands) AddCommand(c, on_command, const_cast<char*>(c));
    nmsc->add_option(nmsc->host, "xtarget_hide_pets", "Hide My Pets in Extended Target", 0, 1, get_xtarget_hide_pets,
                     set_xtarget_hide_pets, nullptr);
    // The patch's own windows (gamepad's More page and the EQ menu's Plugins).
    nmsc->add_menu(nmsc->host, "Personal Loot", "/loot show");
    nmsc->add_menu(nmsc->host, "Autoplay Console", "/autoskill");
    log_info("NMS server detected");
}

void Nms::update_flags() {
    // The patch's CanUseItem / CanEquipItem detours: everything usable while the character has a class mask.
    nmsc->set_flag(nmsc->host, NMSC_FLAG_ITEMS_ANY_CLASS, class_mask() != 0 ? 1 : 0);
}

std::uint32_t Nms::class_mask() const {
    // OP_ServerAuthStats key 1, until it arrives the character set's mask (GameConnection::server_class_mask keeps
    // the same order for what the client shows).
    if (player_mask_) return player_mask_;
    const NmscPlayer p = player();
    auto it = character_masks_.find(lower(p.name));
    return it == character_masks_.end() ? 0 : it->second;
}

std::uint32_t Nms::own_classes() const {
    std::uint32_t classes = class_mask();
    if (classes == 0) {
        const NmscPlayer p = player();
        if (p.class_id > 0) classes = 1u << ((p.class_id - 1) & 31);
    }
    return classes;
}

void Nms::character_sets(const CharacterSets& sets) {
    // Multiclass bit masks from NMS's character sets, by character name, into the core.
    character_sets_seen_ = true;
    for (const auto& [name, mask] : character_masks_) nmsc->set_character_classes(nmsc->host, name.c_str(), 0);
    character_masks_.clear();
    for (const auto& ch : sets.characters) {
        character_masks_[lower(ch.name)] = ch.classes;
        nmsc->set_character_classes(nmsc->host, ch.name.c_str(), ch.classes);
    }
    activate();
    update_flags();
}

void Nms::server_stats(const std::vector<std::pair<std::uint32_t, std::int64_t>>& v) {
    // dinput8.dll 0x10153900 keeps keys 1..0x83; key 0 and the rest are ignored.
    bool any = false;
    for (const auto& [key, value] : v)
        if (key != 0 && key < 0x84) {
            stats[key] = value;
            any = true;
        }
    if (!any) return;
    activate();
    // Key 1 is the patch's class mask (g_cauth_bitmask); 0 falls back to the character set's.
    if (auto it = stats.find(1); it != stats.end()) {
        player_mask_ = static_cast<std::uint32_t>(it->second);
        nmsc->set_player_classes(nmsc->host, player_mask_);
    }
    // FUN_10153300 writes keys 2/5/3/6/4/7 into the profile's current / max HP, mana and endurance, and the Max /
    // Cur detours answer them (§2c): the host's set_vitals does both.
    const Vitals sv = server_vitals(stats);
    NmscVitals n{};
    auto put = [&n](const std::optional<std::int32_t>& f, std::uint32_t bit, std::int32_t& out) {
        if (!f) return;
        n.has |= bit;
        out = *f;
    };
    put(sv.max_hp, NMSC_VITAL_MAX_HP, n.max_hp);
    put(sv.cur_hp, NMSC_VITAL_CUR_HP, n.cur_hp);
    put(sv.max_mana, NMSC_VITAL_MAX_MANA, n.max_mana);
    put(sv.cur_mana, NMSC_VITAL_CUR_MANA, n.cur_mana);
    put(sv.max_endurance, NMSC_VITAL_MAX_ENDURANCE, n.max_endurance);
    put(sv.cur_endurance, NMSC_VITAL_CUR_ENDURANCE, n.cur_endurance);
    if (n.has) nmsc->set_vitals(nmsc->host, &n);
    update_flags();
}

void Nms::zone_server_changed() {
    report_sent_ = warned_ = auth_sent_ = false; // EnterZone re-arms the patch's sends
    zone_in_ms_ = 0;
    cast_class_ = 0;
    waypoints.reset();
    loot.reset(false); // the Personal Loot window hides and says Hello again in the new zone
}

// ---------------------------------------------------------------- packets

bool Nms::packet(int stream, int direction, NmscPacket& p) {
    std::span<std::byte> data(reinterpret_cast<std::byte*>(p.data), p.size);
    if (direction == NMSC_OUT) {
        // The NMS patch's CRC rewrite (nms_packets.h), on an NMS world only (OP_SendCharacterSets came first).
        if (stream == NMSC_STREAM_WORLD && character_sets_seen_ && apply_hardware_id(p.opcode, data, options.hardware_id))
            log_info(std::format("NMS: OP_World_Client_CRC{} carries the fixed hardware id",
                                 p.opcode == op::World_Client_CRC1 ? 1 : 2));
        return false;
    }
    if (p.opcode == op::ClientReport) {
        // The server knows the NMS client patch: one of the signals. Swallowed, as the patch does.
        server_ack_ = true;
        activate();
        return true;
    }
    if (stream == NMSC_STREAM_WORLD) {
        switch (p.opcode) {
        case op::SendCharacterSets:
            if (auto sets = decode_character_sets(data)) character_sets(*sets);
            return true;
        case op::ZoneServerInfo: // a new zone connection starts: the per-zone state resets
            zone_server_changed();
            return false;
        default: return false;
        }
    }
    return zone_packet(p, now_ms());
}

// The HandleWorldMessage detour's opcodes. True = the patch swallows the packet (the original never sees it).
bool Nms::zone_packet(NmscPacket& p, std::int64_t now) {
    const Bytes data(reinterpret_cast<const std::byte*>(p.data), p.size);
    switch (p.opcode) {
    case op::ServerAuthStats:
        if (auto v = decode_server_auth_stats(data)) server_stats(*v);
        return true;
    case op::PetList:
        // The server's list of the player's pets (multi_pet.h): the core keeps it (focus, gauges, the window).
        if (auto list = decode_pet_list(data)) {
            pets_ = *list;
            have_pet_list_ = true;
            std::vector<NmscPet> pets;
            for (const auto& e : pets_) pets.push_back({e.spawn_id, e.class_id});
            nmsc->set_pet_list(nmsc->host, pets.data(), pets.size());
        }
        return true;
    default: break;
    }
    if (!detected_) return false;
    switch (p.opcode) {
    case op::PersonalLootList:
        // The Personal Loot window's OnList (nms_loot.h): the rows a rule covers are answered now.
        if (auto l = decode_personal_loot_list(data)) {
            ensure_loot_rules();
            for (const LootSend& a : loot.on_list(*l, now)) send_loot(a);
        }
        return true;
    case op::PersonalLootFilters:
        // The server's own filters, for clients without the patch: the patch accepts and ignores them.
        return true;
    case op::PersonalLootSettings:
        if (auto s = decode_loot_settings(data)) loot.on_settings(*s);
        return true;
    case op::PersonalLootHistory:
        if (auto h = decode_loot_history(data)) loot.on_history(std::move(*h), now);
        return true;
    case op::SkillTimers:
        // {backstab, bash, kick, strike, frenzy, autoskill mask} -> the patch's cooldown table (0x1035d634). The
        // same handler also runs FUN_100eca90 on this buffer (byte 0: 0 merchant / 1 bank for the spawn id at +1);
        // why is open (notes/nms-dinput8.md §5) and EQEmu's buffer starts with the backstab timer, so the client
        // does not open windows from it.
        if (auto t = decode_skill_timers(data)) {
            skill_timers = *t;
            cooldowns.on_skill_timers(*t, static_cast<std::uint32_t>(now));
        }
        return true;
    case op::Taunt:
        // The patch's cooldown slot 4 (Taunt) := 4 s, then passed on.
        cooldowns.set(4, 4.0f, static_cast<std::uint32_t>(now));
        return false;
    case op::WaypointList:
        // The waypoint window's list (nms_waypoints.h); the patch shows its window on every one.
        waypoints = decode_waypoint_list(data);
        if (waypoints) ++waypoint_serial;
        return true;
    case op::Weblink:
        // The patch opens the URL in its WebView2 or the system browser per /browser (default off: ignored). This
        // client never opens a browser itself: it shows the URL in chat.
        if (auto url = decode_weblink(data)) chat("Web link: " + *url);
        return true;
    case op::DisciplineTimer:
        // The stock SetDisciplineReuseTimer keeps ids 0..19; the patch's detour keeps ids >= 20 in its map (ready =
        // now + seconds). Passed on.
        if (auto t = decode_discipline_timer(data); t && DisciplineTimers::handles(static_cast<std::int32_t>(t->first)))
            disc_timers_.set(static_cast<std::int32_t>(t->first), static_cast<std::uint32_t>(std::time(nullptr)) + t->second);
        return false;
    case op::MulticlassCharSelect:
        // {u32 count; u32 mask[count]} by character select slot; swallowed. The NMS world does not send it (the masks
        // come in OP_SendCharacterSets and OP_SendCharInfo's Deity).
        return true;
    case op::CAuth:
        auth_sent_ = false; // echoed: the server accepted us
        log_info("NMS client check accepted");
        return true;
    default: return false;
    }
}

// ---------------------------------------------------------------- the zone-in and the Heartbeat

void Nms::send_cauth() {
    // FUN_100ed9f0: once per zone, after the class mask and the spawn id are known (EnterZone re-arms it).
    const NmscPlayer me = player();
    send(encode_cauth(own_classes(), me.spawn_id, options.auth_key));
}

void Nms::zoned() {
    // NMS OP_CAuth: the first 8 bytes, XORed with the server key, must read as classes bit mask × own spawn id
    // (zone/client_packet.cpp Handle_OP_CAuth).
    if (!detected_ || options.auth_key == 0 || player().spawn_id == 0) return;
    send_cauth();
    auth_sent_ = true;
    auth_ms_ = now_ms();
}

// The patch's per-frame Heartbeat (FUN_1016b670, §3.3), the parts with client state: the cast spoof, the global
// recovery removal, the OP_CAuth / 0x140f sends and the "non multiclass server" warning.
void Nms::pulse() {
    if (!detected_) return;
    update_flags();
    NmscPlayer me = player();
    if (!in_zone(me)) {
        cast_class_ = 0;
        show_book_pending_ = book_opened_by_scribe_ = false;
        scribe_.reset();
        return;
    }
    const std::int64_t now = now_ms();
    // The client check once more when the server did not echo it within 8 s.
    if (auth_sent_ && options.auth_key && now - auth_ms_ > 8000) {
        send_cauth();
        auth_ms_ = now;
    }
    // The Personal Loot window's OnPulse: its rules for this character, and the Hello once per zone.
    ensure_loot_rules();
    if (auto hello = loot.pulse(player_spawned(me), now)) send_loot(*hello);
    const std::uint32_t mask = class_mask();
    // The cast spoof: while a spell whose Bard level is 255 is being cast, the first mask class that can cast it.
    cast_class_ = 0;
    if (mask && me.casting_spell_id) {
        if (const auto s = spell(me.casting_spell_id); s && s->class_level[kBard - 1] == 0xff) {
            auto orig = [&s](std::uint32_t k) { return s->class_level[k - 1]; };
            cast_class_ = static_cast<std::uint8_t>(cast_spoof_class(orig, mask));
        }
    }
    // SpellCooldownETA (+0x3cc) zeroed whenever no cast is running: no global spell recovery.
    if (!me.casting_spell_id) nmsc->clear_spell_recovery(nmsc->host);
    // The scribe path (§3.4, MQ2Pulse): the book opened for a scroll sits the player down on the next frame,
    // standing up again closes it, and the click is replayed once its 100 ms are up.
    if (show_book_pending_) {
        show_book_pending_ = false;
        if (me.stand_state != 110) command_line("/sit");
        book_opened_by_scribe_ = true;
        me = player(); // the /sit may have changed it
    }
    if (book_opened_by_scribe_ && me.stand_state != 110) {
        book_opened_by_scribe_ = false;
        nmsc->close_spell_book(nmsc->host);
    }
    if (scribe_ && now >= scribe_->due_ms) {
        const PendingScribe p = *scribe_;
        scribe_.reset();
        if (player_spawned(player())) run_scribe(p.slot, p.target_class);
    }
    // The 0x140f report, once after the mask arrived (key 1).
    if (!report_sent_ && stats.count(1) && me.spawn_id != 0) {
        send(encode_client_report(options.report_flags));
        report_sent_ = true;
    }
    // "WARNING! -- You are on a NON Multiclass Server ...": the mask still 0 twenty seconds after the spawn id, and no
    // 0x140f from the server.
    if (zone_in_ms_ == 0) zone_in_ms_ = now;
    if (!warned_ && !server_ack_ && mask == 0 && now - zone_in_ms_ > 20000) {
        warned_ = true;
        chat("WARNING! -- You are on a NON Multiclass Server with the Multiclass Client. Please change servers or "
             "client.");
    }
}

bool Nms::filter_chat(const std::string& text) {
    // The patch's dsp_chat detour (FUN_10119d80): "Your song ends." for a mask without Bard, the vault's lines and the
    // echoes of the vault's and console's own commands ("You say, '#autoskill ...", nms_rules.h hide_patch_line).
    if (!detected_) return false;
    if (text == "Your song ends." && hide_song_ends(class_mask())) return true;
    vault.on_chat(text); // the vault's VAULTDATA lines (FUN_10199a20), hidden below
    return hide_patch_line(text);
}

// ---------------------------------------------------------------- classes and spells

std::optional<std::uint8_t> Nms::spell_level_needed(std::uint32_t spell_id, std::uint32_t class_id) const {
    const std::uint32_t mask = class_mask();
    if (mask == 0) return std::nullopt;
    const auto s = spell(spell_id);
    if (!s) return std::nullopt;
    // The trampoline: EQ_Spell::GetSpellLevelNeeded reads the class's level byte (classes outside 1..16: the warrior
    // byte, as the memorise port reads it).
    auto orig = [&s](std::uint32_t k) -> std::uint8_t { return s->class_level[(k >= 1 && k <= 16 ? k : 1) - 1]; };
    // g_scribeInProgress, or a type-20 scroll of this spell on the cursor.
    const NmscPlayer me = player();
    const auto cursor = item(NmscItemSlot{0, NMSC_SLOT_CURSOR, -1, -1});
    const bool scribe = scribe_in_progress_ || me.scribing_spell_id == spell_id ||
                        (cursor && cursor->item_type == 20 && cursor->scroll_spell == spell_id);
    // The detour reads the spawn's class (PlayerClient +0xeb8), which the Heartbeat's cast spoof may have changed.
    return nms::spell_level_needed(orig, class_id, me.visible_class, mask, player_spawned(me), scribe);
}

bool Nms::own_begin_cast_line(std::uint32_t spell_id) const {
    // The server's 12205 skips bard songs (EQEmu IsBardSong: a Bard level below 255); those keep CastByMe.
    if (class_mask() == 0) return false;
    const auto s = spell(spell_id);
    if (!s || s->class_level[kBard - 1] != 0xff) return false;
    const NmscPlayer me = player();
    return own_begin_cast_dropped(me.filter_pc_spells, me.in_group != 0, me.in_raid != 0);
}

std::int32_t Nms::combat_ability_timer(std::uint32_t spell_id) const {
    if (auto band = discipline_band(spell_id)) return *band;
    const auto s = spell(spell_id);
    return s ? s->timer_id : 0;
}

std::optional<bool> Nms::before_combat_ability(std::uint32_t spell_id) {
    // The doCombatAbility detour (FUN_10109820) for a timer id >= 20: refused while its map timer runs.
    combat_now_ = static_cast<std::uint32_t>(std::time(nullptr));
    const std::int32_t timer = combat_ability_timer(spell_id);
    if (!DisciplineTimers::handles(timer) || disc_timers_.ready(timer, combat_now_)) return std::nullopt;
    const auto s = spell(spell_id);
    chat(discipline_not_ready_line(s ? s->name : "", disc_timers_.remaining(timer, combat_now_)));
    return false;
}

void Nms::after_combat_ability(std::uint32_t spell_id, bool used) {
    // ... and stamped now + RecastTime / 1000 when the original used it.
    const std::int32_t timer = combat_ability_timer(spell_id);
    if (!used || !DisciplineTimers::handles(timer)) return;
    const auto s = spell(spell_id);
    disc_timers_.set(timer, combat_now_ + (s ? s->recast_time_ms / 1000 : 0));
}

std::string Nms::combat_effect_label() const {
    // The CCombatAbilityWnd OnProcessFrame vtable hook (§3.3): the label names the first buff whose spell has a
    // timer id >= 20, else "No Effect".
    const NmscPlayer me = player();
    for (std::uint32_t i = 0; i < me.buff_slots; ++i) {
        NmscBuff b{};
        b.size = sizeof b;
        if (!nmsc->get_buff(nmsc->host, i, &b) || b.spell_id == 0) continue;
        if (DisciplineTimers::handles(combat_ability_timer(b.spell_id)))
            if (const auto s = spell(b.spell_id)) return s->name;
    }
    return "No Effect";
}

// ---------------------------------------------------------------- the extended target window

bool Nms::hide_xtarget_slot(int slot) const {
    if (!options.xtarget_hide_pets || slot < 0) return false;
    NmscXTarget x{};
    x.size = sizeof x;
    if (!nmsc->get_xtarget(nmsc->host, static_cast<std::uint32_t>(slot), &x)) return false;
    const auto s = spawn(x.spawn_id);
    std::vector<std::uint32_t> pets;
    for (const auto& p : pets_) pets.push_back(p.spawn_id);
    return xtarget_server_pet_slot(x.type, x.spawn_id, s ? s->owner_id : 0, player().spawn_id, pets);
}

// ---------------------------------------------------------------- the scribe path

// The patch's CInvSlot::HandleRButtonUp detour (§3.4): a right click on a scroll or tome scribes it, opening the
// spell book and sitting first unless it is a tome or the book is open (the Heartbeat replays the click 100 ms
// later). The class / level 100 spoof around the original reaches its casting check only.
bool Nms::item_right_click(const NmscItemSlot& slot) {
    const NmscPlayer me = player();
    if (!in_zone(me) || !player_spawned(me)) return false;
    const auto it = item(slot);
    if (!it || !scribe_click(static_cast<std::uint8_t>(it->item_type))) return false;
    const std::uint8_t target = scribe_class(it->classes, class_mask());
    // A tome learns at once, the book closed; with the book open a scroll scribes at once too.
    if (scribe_is_tome(static_cast<std::uint8_t>(it->item_type), it->name) || me.spell_book_open) {
        run_scribe(slot, target);
        return true;
    }
    // Else: /book now, the sit on the next frame, the click again 100 ms later.
    command_line("/book");
    show_book_pending_ = true;
    scribe_ = PendingScribe{slot, target, now_ms() + kScribeDelayMs};
    return true;
}

void Nms::run_scribe(const NmscItemSlot& slot, std::uint8_t target_class) {
    // NMS_BeginScribeSpoof: class = the target class (when there is one) and level 100 on the spawn and profile,
    // g_scribeInProgress; then the original HandleRButtonUp, then everything back. Of the spoof only the class
    // reaches the original (its casting check's BardCastBard); the item type 31 the tome path also writes is not read
    // there (rof2decomp export CInvSlot.c and the disassembly @0x697250).
    scribe_in_progress_ = true;
    nmsc->right_click_item(nmsc->host, &slot, target_class);
    scribe_in_progress_ = false;
}

// ---------------------------------------------------------------- commands

void Nms::command(const std::string& word, const std::string& args) {
    if (word == "/nmsloot") loot_command(args); // the shipped patch's name for its loot window's command
    else if (word == "/fov" || word == "/camera") { // the patch's CmdFov: the max zoom distance
        float max = nmsc->max_camera_distance(nmsc->host);
        const float before = max;
        chat(fov_command(args, max));
        if (max != before) nmsc->set_max_camera_distance(nmsc->host, max);
    } else if (word == "/autoskill") autoskill_command(args); // the patch's console (FUN_1016c310)
    else if (word == "/waypoint" || word == "/waypoints") waypoint_command(args);
    else if (word == "/pets") pets_command(); // MultiPet's commands (multi_pet.cpp)
    else if (word == "/petcycle") petcycle_command();
    else if (word == "/petdebug") petdebug_command();
}

bool Nms::game_command(const std::string& handler, const std::string& args) {
    if (handler == "__do_loot") {
        // The patch's AddCommand("/loot", LootCommand) takes /loot over: its Personal Loot window.
        loot_command(args);
        return true;
    }
    if (handler != "cmd_alternateadv") return false;
    // The NMS patch's detour (cmd_alternateadv, Hooks.cpp): "/alt toggle <id>" becomes "/say #alttoggle <id>" (an NMS
    // server command); other forms go on to the original.
    std::string_view rest = args;
    std::string first = lower(next_arg(rest));
    std::string second = next_arg(rest);
    if (first != "toggle") return false;
    bool numeric = !second.empty() && second.size() <= 10 &&
                   std::all_of(second.begin(), second.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    if (!numeric) chat("Usage: /alt toggle <AA Id>");
    else command_line("/say #alttoggle " + second);
    return true;
}

void Nms::send_loot(const LootSend& a) {
    // The patch's Send: only in the game, on the zone connection.
    if (!in_zone(player())) return;
    send(encode_personal_loot_action(a.action, a.pending_id, a.item_id, a.value));
}

void Nms::ensure_loot_rules() {
    // LoadRulesIfNeeded: the character's itemrules.ini (the patch's <name>_itemrules.ini), read when the character
    // changes. (The patch also rereads while it has no rules, every pulse; "/loot rules" rereads a hand edit.)
    const NmscPlayer me = player();
    const std::string who = me.name;
    if (who == loot_rules_path_) return;
    loot_rules_path_ = who;
    const fs::path dir = who.empty() ? fs::path() : host_dir(nmsc->character_config_dir);
    if (dir.empty()) {
        loot.rules().load({});
        return;
    }
    const fs::path file = dir / "itemrules.ini";
    // Before the plugin, the client kept it in the character's own folder: moved here once.
    std::error_code ec;
    const fs::path before = dir.parent_path().parent_path() / "itemrules.ini";
    if (!fs::exists(file, ec) && fs::exists(before, ec)) {
        fs::copy_file(before, file, ec);
        if (!ec) fs::remove(before, ec);
    }
    loot.rules().load(file);
}

void Nms::loot_command(const std::string& args) {
    ensure_loot_rules();
    LootCommandResult r = nms::loot_command(loot, args, now_ms());
    for (const auto& line : r.lines) chat(line);
    for (const auto& a : r.sends) send_loot(a);
    if (r.reset_window) ++loot_reset_serial;
}

void Nms::send_waypoint_request(const WaypointRequest& r) {
    if (in_zone(player())) send(encode(r));
}

void Nms::waypoint_command(const std::string& args) {
    // No argument opens the waypoint window, "list" lists the waypoints in chat, an id travels there.
    if (!waypoints) {
        chat("No waypoint list from the server yet: click the Magic Map or a waypoint.");
        return;
    }
    if (args.empty()) {
        ++waypoint_serial; // the window shows again
        return;
    }
    if (args == "list") {
        for (const auto& e : waypoints->entries)
            chat(std::to_string(e.id) + ": " + e.name + (e.enabled ? "" : " (locked)"));
        return;
    }
    WaypointRequest r;
    r.id = std::atoi(args.c_str());
    r.group = waypoints->group_selected;
    r.autoconfirm = waypoints->autoconfirm_selected;
    send_waypoint_request(r);
}

void Nms::autoskill_command(const std::string& args) {
    // FUN_1016c310: no argument toggles the console and says so; anything else goes to the server's #autoskill.
    std::size_t i = 0;
    while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
    if (i == args.size()) {
        autoplay_console = !autoplay_console.value_or(false);
        chat(std::string("[NMS] Multiclass Autoplay Console ") + (*autoplay_console ? "Enabled." : "Disabled."));
        return;
    }
    command_line("#autoskill " + args.substr(i));
}

void Nms::pets_command() const {
    // MultiPet::ListPets: the spawns' own names ("(spawning)" before the spawn is here), the summoning class and the
    // pet in the pet window (the local player's PetID).
    if (!have_pet_list_ || pets_.empty()) {
        chat("You have no pets.", kConYellow);
        return;
    }
    const std::uint32_t focused = player().pet_id;
    chat(std::format("You have {} pet(s):", pets_.size()), kConYellow);
    for (std::size_t i = 0; i < pets_.size(); ++i) {
        const auto& pet = pets_[i];
        const auto s = spawn(pet.spawn_id);
        std::string line = std::format("  {}: {}", i + 1, s ? std::string(s->name) : std::string("(spawning)"));
        if (pet.class_id > 0) line += std::format(" [{}]", pet_class_name(pet.class_id));
        if (pet.spawn_id == focused) line += " (pet window)";
        chat(line, kConGreen);
    }
}

void Nms::petcycle_command() const {
    // MultiPet::CyclePet: the pet after the window's in the list (the first when the window's is not listed) goes
    // into the pet window; only the local PetID changes, the server is not told.
    if (pets_.empty()) return;
    const std::uint32_t focused = player().pet_id;
    std::size_t i = 0;
    while (i < pets_.size() && pets_[i].spawn_id != focused) ++i;
    nmsc->set_window_pet(nmsc->host, pets_[i >= pets_.size() ? 0 : (i + 1) % pets_.size()].spawn_id);
}

void Nms::petdebug_command() const {
    // MultiPet::DebugSpawns (the spawn pointers are this client's spawn table here).
    const NmscPlayer me = player();
    chat(std::format("MultiPet Debug: localID={}  uiPetID={}  havePetList={}  trackedPets={}", me.spawn_id, me.pet_id,
                     have_pet_list_ ? 1 : 0, pets_.size()),
         kConYellow);
    for (std::size_t i = 0; i < pets_.size(); ++i) {
        const auto& pet = pets_[i];
        const auto s = spawn(pet.spawn_id);
        chat(std::format("  Pet[{}]: spawnID={} name='{}' classID={} spawned={}", i, pet.spawn_id,
                         s ? std::string(s->name) : std::string(), pet.class_id, s ? "yes" : "no"),
             kConGreen);
    }
    std::size_t count = 0;
    NmscSpawn s{};
    s.size = sizeof s;
    for (std::uint32_t id = 0; nmsc->next_spawn(nmsc->host, id, &s); id = s.id) ++count;
    chat(std::format("  Total spawns iterated: {}", count), kConGrey);
}

std::vector<std::string> Nms::who_lines(const NmscWho& who) const {
    // The NMS patch replaces the whole reply with its own lines (FUN_10198eb0, nms_rules.h).
    std::vector<WhoEntry> entries;
    for (std::uint32_t i = 0; i < who.count; ++i) {
        const NmscWhoEntry* e = NMSC_WHO_ENTRY(&who, i);
        entries.push_back({e->name ? e->name : "", e->class_id, e->level, e->race, e->zone_id, e->format,
                           e->rank_string, e->zone_string});
    }
    return format_who(entries, zone_name);
}

// ---------------------------------------------------------------- the hook table

namespace {

// Strings the hooks hand the core: copied before the call returns.
std::string g_text;
const char* text_out(std::optional<std::string> s) {
    if (!s) return nullptr;
    g_text = std::move(*s);
    return g_text.c_str();
}

int h_game_command(const char* handler, const char* args) {
    return g_nms.game_command(handler ? handler : "", args ? args : "") ? 1 : 0;
}
int h_who_lines(const NmscWho* who, NmscLineFn add, void* ctx) {
    for (const auto& line : g_nms.who_lines(*who)) add(ctx, line.c_str());
    return 1;
}
std::uint8_t h_cast_class() { return g_nms.cast_class(); }
int h_is_spellcaster(int* out) {
    // The patch's IsSpellcaster detours answer (mask & 0x7ebe) != 0 for the local character.
    const std::uint32_t mask = g_nms.class_mask();
    if (!mask) return 0;
    *out = is_spellcaster(mask) ? 1 : 0;
    return 1;
}
int h_spell_level_needed(std::uint32_t spell_id, std::uint32_t class_id, std::uint8_t* out) {
    auto v = g_nms.spell_level_needed(spell_id, class_id);
    if (!v) return 0;
    *out = *v;
    return 1;
}
int h_song_blocked(std::uint8_t bard_level) {
    // The patch's BardCastBard detour: a spell whose Bard level byte (+0x24e) is 255 is never a song.
    return g_nms.class_mask() != 0 && bard_cast_bard_blocked(bard_level) ? 1 : 0;
}
int h_own_begin_cast_line(std::uint32_t spell_id) { return g_nms.own_begin_cast_line(spell_id) ? 1 : 0; }
int h_can_use_gem(int gem, int* out) {
    // The patch's CanUseMemorizedSpellSlot detour: gem < 12, from level 1.
    if (g_nms.class_mask() == 0) return 0;
    *out = can_use_memorized_spell_slot(gem) ? 1 : 0;
    return 1;
}
int h_ability_timer_id(std::uint32_t spell_id, std::int32_t* out) {
    // The patch's discipline timers (§2a): the band table moves CARecastTimerID.
    auto band = discipline_band(spell_id);
    if (!band) return 0;
    *out = *band;
    return 1;
}
// The GetDisciplineReuseTimer detour (FUN_10109780): ids >= 20 from its map, 0 (ready) when absent.
int h_discipline_reuse(std::int32_t timer_id, std::uint32_t* out) {
    auto v = g_nms.discipline_reuse(timer_id);
    if (!v) return 0;
    *out = *v;
    return 1;
}
int h_before_combat_ability(std::uint32_t spell_id, int* allowed) {
    auto v = g_nms.before_combat_ability(spell_id);
    if (!v) return 0;
    *allowed = *v ? 1 : 0;
    return 1;
}
void h_after_combat_ability(std::uint32_t spell_id, int used) { g_nms.after_combat_ability(spell_id, used != 0); }
const char* h_combat_effect_label() { return text_out(g_nms.combat_effect_label()); }
// EQEmu stores NMS multiclass guild members as 1000 + the class mask.
std::uint32_t h_guild_member_classes(std::uint32_t value) { return value >= 1000 ? value - 1000 : 0; }
std::uint32_t h_vitals_classes(std::uint8_t class_id) {
    // NMS multiclass servers take the best of the unlocked classes (zone/client_mods.cpp CalcBaseHP, CalcBaseMana,
    // CalcBaseEndurance).
    const std::uint32_t mask = g_nms.class_mask();
    if (!mask || mask == class_bit(class_id)) return 0;
    return mask & 0xffffu;
}
// The HasSkill / IsAvailable detours (§3.8) and the tracking window's rule.
int h_skill_gate(std::int32_t skill, std::uint32_t mask, std::int32_t profile_skill, int* has) {
    auto v = skill_gate(skill, mask, profile_skill);
    if (!v) return 0;
    *has = *v ? 1 : 0;
    return 1;
}
std::uint32_t h_skill_spoof_class(std::int32_t skill, std::uint32_t mask) { return skill_spoof_class(skill, mask); }
int h_tracking_allowed(std::uint8_t class_id, std::uint32_t mask, std::uint32_t skill, int* out) {
    *out = tracking_allowed(class_id, mask, skill) ? 1 : 0;
    return 1;
}
const char* h_item_level_line(std::uint32_t spell_id) { return text_out(g_nms.item_level_line(spell_id)); }
const char* h_label(int id, const char* text) { return text_out(g_nms.label(id, text ? text : "")); }
int h_hide_xtarget_slot(int slot) { return g_nms.hide_xtarget_slot(slot) ? 1 : 0; }
int h_item_right_click(const NmscItemSlot* slot) { return slot && g_nms.item_right_click(*slot) ? 1 : 0; }
const char* h_list_level(const NmscItem* item) { return item ? text_out(g_nms.list_level(*item)) : nullptr; }

} // namespace

} // namespace nms

// ---------------------------------------------------------------- the exports

NMSC_API void InitializePlugin(void) { nms::plugin().initialize(); }
NMSC_API void ShutdownPlugin(void) { nms::plugin().shutdown(); }
NMSC_API void OnPulse(void) { nms::plugin().pulse(); }
NMSC_API void OnZoned(void) { nms::plugin().zoned(); }
NMSC_API int OnFilterChat(const char* line, uint32_t) { return line && nms::plugin().filter_chat(line) ? 1 : 0; }
NMSC_API int OnPacket(int stream, int direction, NmscPacket* p) {
    return p && nms::plugin().packet(stream, direction, *p) ? 1 : 0;
}

NMSC_API const NmscHooks* nmsc_plugin_hooks(void) {
    using namespace nms;
    static NmscHooks hooks;
    std::memset(&hooks, 0, sizeof hooks);
    hooks.size = sizeof hooks;
    hooks.game_command = h_game_command;
    hooks.who_lines = h_who_lines;
    hooks.cast_class = h_cast_class;
    hooks.is_spellcaster = h_is_spellcaster;
    hooks.spell_level_needed = h_spell_level_needed;
    hooks.song_blocked = h_song_blocked;
    hooks.own_begin_cast_line = h_own_begin_cast_line;
    hooks.can_use_gem = h_can_use_gem;
    hooks.ability_timer_id = h_ability_timer_id;
    hooks.discipline_reuse = h_discipline_reuse;
    hooks.before_combat_ability = h_before_combat_ability;
    hooks.after_combat_ability = h_after_combat_ability;
    hooks.combat_effect_label = h_combat_effect_label;
    hooks.guild_member_classes = h_guild_member_classes;
    hooks.vitals_classes = h_vitals_classes;
    hooks.skill_gate = h_skill_gate;
    hooks.skill_spoof_class = h_skill_spoof_class;
    hooks.tracking_allowed = h_tracking_allowed;
    hooks.item_level_line = h_item_level_line;
    hooks.label = h_label;
    hooks.hide_xtarget_slot = h_hide_xtarget_slot;
    hooks.item_right_click = h_item_right_click;
    hooks.list_level = h_list_level;
    return &hooks;
}
