#pragma once

// The NMS module: what the NMS server's client (NMS/dinput8.dll, the patch the NMS server ships with the game) does
// differently from the stock RoF2 client, as a server extension (game/server_ext.h). Built into the client; the
// app hands one to each connection (net::ConnectionOptions::extension) and the connection turns it on when it
// detects an NMS server (GameConnection::nms_mode(): the world sent OP_SendCharacterSets, or the zone
// OP_ServerAuthStats or 0x140f). On any other server none of this runs. docs/nms-module.md.
//
// The rules are in plugins/nms/nms_rules.h, the patch's packets in plugins/nms/nms_packets.h; this class keeps the
// patch's state and answers the core's hooks (nms_extension.cpp: the connection and classes; nms_labels.cpp: the
// Lua windows' values; nms_ui.cpp: its commands and ImGui windows; nms_lua.cpp: its Lua windows).

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/server_profile.h"
#include "game/server_ext.h"
#include "net/item_packets.h"
#include "plugins/nms/nms_loot.h"
#include "plugins/nms/nms_packets.h"
#include "plugins/nms/nms_rules.h"
#include "plugins/nms/nms_vault.h"
#include "plugins/nms/nms_waypoints.h"

namespace nmsc { class Config; }
namespace nmsc::net { struct ConnectionOptions; }

namespace nmsc::nms {

// The key the current NMS patch compiles into its OP_CAuth sender (NMS-Release eqgame_dll/eqgame.cpp
// FireCAuthHandshake: private_key = 352236586), the server's Custom:ServerAuthKey default (common/ruletypes.h).
inline constexpr std::uint32_t kPatchAuthKey = 352236586;

struct NmsOptions {
    // NMS servers with Custom:ServerAuthStats move clients that do not answer
    // the OP_CAuth check to the Bazaar; the key is that server's
    // Custom:ServerAuthKey. The default is the current patch's own key
    // (kPatchAuthKey); 0 = do not answer (fine for GM accounts).
    std::uint32_t auth_key = kPatchAuthKey;
    // The NMS patch's login CRC rewrite (plugins/nms/nms_packets.h): Fixed sends its VM values in place of the
    // eqgame.exe and SkillCaps.txt checksums on an NMS world; None sends the stock checksums. Never real hardware ids.
    HardwareIdMode hardware_id = HardwareIdMode::Fixed;
    // The flags of the patch's 0x140f report (plugins/nms/nms_packets.h): not launcher-started, channel 'L',
    // large-address aware, as the shipped eqgame.exe (LAA) under the shipped DLL reports when started directly.
    std::uint32_t report_flags = kReportLargeAddress;
    // Hailstone: the extended target window leaves out the player's own pets the server puts in its last slots
    // (nms::xtarget_server_pet_slot); the client option nms_xtarget_hide_pets, on by default.
    bool xtarget_hide_pets = true;
};
// hailstone.ini: nms.auth_key, nms.hardware_id = fixed (default) | none, nms.xtarget_hide_pets (default true).
NmsOptions read_options(const Config& config);
// The same with a saved server's own [NMS] auth_key and hardware_id (core/server_profile.h) over the global keys.
NmsOptions read_options(const Config& config, const ServerProfile& profile);
// A connection's server module from a saved server's module setting: auto hands it the NMS extension, on once the
// connection detects an NMS server (today's behaviour); nms the same, on from the start (force_extension); none no
// extension, the stock client throughout.
void set_server_module(net::ConnectionOptions& options, ServerModule module, const NmsOptions& nms);

// The Personal Loot window's Loot tab in ImGui, for a UI without the Lua one (nmsloot.lua); returns the action to
// send.
std::optional<LootSend> loot_window(PersonalLoot& loot, bool reset, ui::ItemIconCache* icons);

class NmsExtension final : public game::ServerExtension {
public:
    explicit NmsExtension(NmsOptions options = {}) : options_(options) {}
    const char* name() const override { return "nms"; }
    // The NMS client patch and server write item ids with 8 hex digits (SAY_LINK_BODY_SIZE 77).
    bool wide_item_links() const override { return true; }

    // ---- the connection (nms_extension.cpp) ----
    bool world_packet(net::GameConnection& c, const net::AppPacket& p) override { return packet(c, p); }
    bool zone_packet(net::GameConnection& c, const net::AppPacket& p) override { return packet(c, p); }
    void outgoing_checksum(net::GameConnection& c, net::AppPacket& p) override;
    void server_stats_changed(net::GameConnection& c) override;
    void zone_server_changed(net::GameConnection& c) override;
    void zone_entered(net::GameConnection& c) override;
    bool retry_zone_entry(net::GameConnection& c) override;
    void heartbeat(net::GameConnection& c) override;
    bool command(net::GameConnection& c, const std::string& word, const std::string& args) override;
    bool handler_command(net::GameConnection& c, std::string_view handler, const std::string& args) override;
    bool hide_chat_line(net::GameConnection& c, const std::string& text) override;
    std::optional<std::vector<std::string>> who_lines(const net::GameConnection& c,
                                                      const net::WhoResponse& r) const override;

    // ---- classes (nms_extension.cpp) ----
    // The patch's class mask (g_cauth_bitmask): the core's (GameConnection::server_class_mask), which this extension
    // sets from OP_ServerAuthStats key 1 and the character sets (OP_SendCharacterSets).
    std::uint32_t class_mask(const net::GameConnection& c) const;
    std::uint8_t cast_class(const net::GameConnection&) const override { return cast_class_; }
    std::optional<bool> is_spellcaster(const net::GameConnection& c) const override;
    std::optional<std::uint8_t> spell_level_needed(const net::GameConnection& c, const game::SpellData& spell,
                                                   std::uint32_t class_id) const override;
    bool song_blocked(const net::GameConnection& c, std::uint8_t raw_bard_level) const override;
    bool own_begin_cast_line(const net::GameConnection& c, const game::SpellData& s) const override;
    std::optional<bool> can_use_gem(const net::GameConnection& c, int gem) const override;
    // The patch's discipline timers (§2a): the band table moves CARecastTimerID, ids >= 20 run on its own map.
    std::int32_t combat_ability_timer(const net::GameConnection& c, const game::SpellData& s) const override;
    std::optional<std::uint32_t> discipline_reuse_timer(const net::GameConnection& c, std::int32_t id) const override;
    std::optional<bool> before_combat_ability(net::GameConnection& c, const game::SpellData& s) override;
    void after_combat_ability(net::GameConnection& c, const game::SpellData& s, bool used) override;
    // The CCombatAbilityWnd OnProcessFrame vtable hook (§3.3): the label names the first buff whose spell has a
    // timer id >= 20, else "No Effect".
    std::optional<std::string> combat_effect_label(const net::GameConnection& c) const override;
    bool can_use_all_items(const net::GameConnection& c) const override { return class_mask(c) != 0; }
    // EQEmu stores NMS multiclass guild members as 1000 + the class mask.
    std::uint32_t guild_member_classes(const net::GameConnection&, std::uint32_t class_value) const override {
        return class_value >= 1000 ? class_value - 1000 : 0;
    }
    // The patch NOPs the class test (0x45ea0f, 75 10 -> 90 90): moving breaks Hide unless sneaking, any class.
    std::uint8_t hide_check_class(const net::GameConnection&, std::uint8_t) const override { return 9; }
    bool aa_any_level(const net::GameConnection&) const override { return true; }
    bool aa_train_all(const net::GameConnection&) const override { return true; }
    std::vector<std::uint8_t> max_vitals_classes(const net::GameConnection& c, std::uint8_t class_id) const override;
    // The Max / Cur HP, mana and endurance detours (§2c): the server's values (nms::server_vitals).
    game::CharacterVitals character_vitals(const net::GameConnection& c) const override;
    const game::SkillRules* skill_rules() const override;
    // The server's pet slots (nms::xtarget_server_pet_slot), with nms_xtarget_hide_pets on.
    bool hide_xtarget_slot(const net::GameConnection& c, int slot) const override;
    // nms_xtarget_hide_pets (hailstone.ini nms.xtarget_hide_pets).
    std::optional<int> option(std::string_view name) const override;
    std::optional<std::pair<std::string, std::string>> set_option(std::string_view name, int value) override;
    // The patch's CInvSlot::HandleRButtonUp detour (§3.4): a right click on a scroll or tome scribes it, opening the
    // spell book and sitting first unless it is a tome or the book is open (heartbeat replays the click 100 ms
    // later). The class / level 100 spoof around the original reaches its casting check only (class_id).
    bool item_right_click(net::GameConnection& c, const net::ItemSlot& slot) override;

    // ---- the Lua windows' values (nms_labels.cpp) ----
    std::function<std::string(const game::SpellData&)> level_needed_line(const net::GameConnection& c) const override;
    std::optional<std::string> merchant_level(const net::GameConnection& c, const net::Item& item) const override;
    void label_text(const net::GameConnection& c, int type, std::string& text) const override;

    // ---- commands and windows (nms_ui.cpp) ----
    bool window_toggle(net::GameConnection& c, const std::string& toggle, game::ExtensionAppContext& app) override;
    void draw(net::GameConnection& c, game::ExtensionAppContext& app) override;
    // /loot and /nmsloot: the Personal Loot window's commands (plugins/nms/nms_loot.h loot_command).
    void loot_command(net::GameConnection& c, const std::string& args);
    // The patch's /fov (and /camera) <10..150>: the maximum camera zoom distance; returns the line to print.
    static std::string fov_command(const std::string& args, float& max_camera_distance, float& camera_distance);

    // ---- the patch's state ----
    // OP_SkillTimers (0x1339): the server's combat skill timers (seconds left when received) and the autoskill mask.
    const SkillTimers& skill_timers() const { return skill_timers_; }
    // The patch's cooldown table over them (and OP_Taunt), for its cooldown window (plugins/nms/nms_lua.cpp).
    const Cooldowns& cooldowns() const { return cooldowns_; }
    // OP_WaypointList (0x1402): the waypoints; travel with request_waypoint (OP_WaypointRequest). Each list shows
    // the patch's waypoint window again (FUN_101954a0): waypoint_serial() counts them (and /waypoint's opens).
    const std::optional<WaypointList>& waypoints() const { return waypoints_; }
    std::uint32_t waypoint_serial() const { return waypoint_serial_; }
    // `expedition`: the window's My Expedition (id 0); id 0 alone only stores the Port group / Auto accept boxes.
    void request_waypoint(net::GameConnection& c, std::int32_t id, bool expedition = false);
    // OP_WaypointRequest as given (the Lua WaypointsWnd's buttons and check boxes, with its own box states).
    void send_waypoint_request(net::GameConnection& c, const WaypointRequest& r);
    // /waypoint [list|id]: opens the waypoint window (plugins/nms/nms_waypoints.h), lists the waypoints, or travels.
    void waypoint_command(net::GameConnection& c, const std::string& args);
    // The add-on's MultiPet commands (multi_pet.cpp) over the server's pet list (OP_PetList): /pets lists the pets,
    // /petcycle shows the next one in the pet window, /petdebug says what is tracked.
    static void pets_command(net::GameConnection& c);
    static void petcycle_command(net::GameConnection& c);
    static void petdebug_command(net::GameConnection& c);
    // The patch's discipline timers for ids >= 20 (Get/SetDisciplineReuseTimer detours), ready in Unix seconds.
    const DisciplineTimers& discipline_timers() const { return disc_timers_; }
    const NmsOptions& options() const { return options_; }
    // The Personal Loot window (plugins/nms/nms_loot.h) and an OP_PersonalLootAction from it.
    PersonalLoot& personal_loot() { return loot_; }
    void send_loot(net::GameConnection& c, const LootSend& a);
    // The "Multiclass Autoplay Console" (FUN_1000a430): shown or not (unset until the UI gives its saved state);
    // /autoskill with no argument toggles it.
    std::optional<bool> autoplay_console;
    // /autoskill [args]: toggles the console, or sends "#autoskill <args>" (FUN_1016c310).
    void autoskill_command(net::GameConnection& c, const std::string& args);
    // The Dimensional Pocket Vault (plugins/nms/nms_vault.h), fed by the chat lines the dsp_chat detour reads.
    Vault& vault() { return vault_; }
    // /loot reset counts here: the window goes back to its default place and size.
    std::uint32_t loot_reset_serial() const { return loot_reset_serial_; }

private:
    bool packet(net::GameConnection& c, const net::AppPacket& p);
    // OP_SendCharacterSets: the characters' class masks into the core (GameConnection::set_character_classes).
    void character_sets_changed(net::GameConnection& c);
    void send_cauth(net::GameConnection& c);
    void ensure_loot_rules(net::GameConnection& c);

    NmsOptions options_;
    bool report_sent_ = false;     // our 0x140f this zone (re-armed by the next zone)
    std::uint8_t cast_class_ = 0;  // the Heartbeat's cast spoof, 0 none
    SkillTimers skill_timers_;
    Cooldowns cooldowns_;
    DisciplineTimers disc_timers_;
    std::uint32_t combat_now_ = 0; // the doCombatAbility detour's time, taken before the original runs
    std::optional<WaypointList> waypoints_;
    std::uint32_t waypoint_serial_ = 0;
    std::int64_t zone_in_ms_ = 0;  // for the "non multiclass server" warning
    bool warned_ = false;
    bool auth_sent_ = false;       // OP_CAuth sent this zone and not echoed yet
    std::chrono::steady_clock::time_point auth_time_{};
    // The scribe path's state (§3.4): g_pendingSpellBookShow, g_spellBookOpenedByScribe, the pending replay
    // (g_pendingScribeThis as the slot, its target class, g_scribeEndTick) and g_scribeInProgress, which makes
    // GetSpellLevelNeeded answer as for a scroll on the cursor while the original runs.
    struct PendingScribe {
        net::ItemSlot slot;
        std::uint8_t target_class = 0;
        std::int64_t due_ms = 0;
    };
    void run_scribe(net::GameConnection& c, const net::ItemSlot& slot, std::uint8_t target_class);
    bool show_book_pending_ = false;
    bool book_opened_by_scribe_ = false;
    std::optional<PendingScribe> scribe_;
    bool scribe_in_progress_ = false;
    PersonalLoot loot_;
    Vault vault_;
    std::uint32_t loot_reset_serial_ = 0;
    std::uint32_t loot_reset_seen_ = 0; // the ImGui window's
    WaypointWindowState waypoint_window_;
};

} // namespace nmsc::nms
