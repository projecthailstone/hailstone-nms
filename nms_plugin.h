#pragma once

// The NMS plugin: what the NMS server's client (NMS/dinput8.dll, the patch the NMS server ships with the game) does
// differently from the stock RoF2 client, as a Hailstone plugin built only against the plugin SDK
// (sdk/hailstone_plugin.h). docs/plugins/nms.md.
//
// It may load on any server (a player's copy in plugins/nms/, or the server's own), so it starts inactive
// (set_active(0)): none of its hooks, switches or commands run until it sees an NMS server, which is the world's
// OP_SendCharacterSets, the zone's OP_ServerAuthStats, or 0x140f from either. Then it answers the core's hooks,
// turns on the patch's rule switches, feeds the core the class masks, the pet list and the server's vitals, and adds
// its commands, option and menu entries. Its windows are Lua for each UI pack (ui/<pack>/), fed by the functions in
// nms_ui.cpp through eq.Plugin("nms").
//
// The rules are in nms_rules.h and the packets in nms_packets.h; this keeps the patch's state (nms_plugin.cpp: the
// packets, hooks and commands; nms_labels.cpp: the labels and item lines; nms_ui.cpp: the windows' functions).

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nms_loot.h"
#include "nms_options.h"
#include "nms_packets.h"
#include "nms_petcmd.h"
#include "nms_rules.h"
#include "nms_vault.h"
#include "nms_waypoints.h"
#include "hailstone_plugin.h"

namespace nms {

// The patch's state on this connection. One per loaded plugin (the client loads it again for each login).
class Nms {
public:
    // ---- the plugin's life (nms_plugin.cpp) ----
    void initialize();
    void shutdown();
    // OnPacket: true drops the packet.
    bool packet(int stream, int direction, HailstonePacket& p);
    void pulse();
    void zoned();
    // OnFilterChat: true hides the line.
    bool filter_chat(const std::string& text);

    bool detected() const { return detected_; }
    // The patch's class mask (g_cauth_bitmask): OP_ServerAuthStats key 1, until it arrives the character set's mask
    // for the current character; 0 on a server without multiclassing.
    std::uint32_t class_mask() const;
    // The classes the character's windows filter by: the mask, else the profile's class.
    std::uint32_t own_classes() const;

    // ---- the core's hooks (nms_plugin.cpp, nms_labels.cpp) ----
    std::uint8_t cast_class() const { return cast_class_; }
    std::optional<std::uint8_t> spell_level_needed(std::uint32_t spell_id, std::uint32_t class_id) const;
    bool own_begin_cast_line(std::uint32_t spell_id) const;
    // The spell's CARecastTimerID: the patch's band, else the file's.
    std::int32_t combat_ability_timer(std::uint32_t spell_id) const;
    std::optional<std::uint32_t> discipline_reuse(std::int32_t timer_id) const {
        if (!DisciplineTimers::handles(timer_id)) return std::nullopt;
        return disc_timers_.get(timer_id);
    }
    std::optional<bool> before_combat_ability(std::uint32_t spell_id);
    void after_combat_ability(std::uint32_t spell_id, bool used);
    std::string combat_effect_label() const;
    bool hide_xtarget_slot(int slot) const;
    bool item_right_click(const HailstoneItemSlot& slot);
    bool game_command(const std::string& handler, const std::string& args);
    std::vector<std::string> who_lines(const HailstoneWho& who) const;
    std::optional<std::string> label(int type, const std::string& text) const;
    std::optional<std::string> item_level_line(std::uint32_t spell_id) const;
    std::optional<std::string> list_level(const HailstoneItem& item) const;

    // ---- commands (nms_plugin.cpp) ----
    void command(const std::string& word, const std::string& args);
    void loot_command(const std::string& args);
    void waypoint_command(const std::string& args);
    void autoskill_command(const std::string& args);
    void pets_command() const;
    void petcycle_command() const;
    void petdebug_command() const;
    const std::vector<PetListEntry>& pets() const { return pets_; } // OP_PetList, as it came

    // ---- for the windows (nms_ui.cpp) ----
    void send_loot(const LootSend& a);
    void send_waypoint_request(const WaypointRequest& r);
    void save_options();

    Options options;
    PersonalLoot loot;
    Vault vault;
    PetCommands pet_commands; // the Pet window's #petcmd lines (nms_petcmd.h)
    SkillTimers skill_timers;
    std::optional<WaypointList> waypoints;
    std::uint32_t waypoint_serial = 0; // each list shows the window again (0x101954a0), and /waypoint
    std::uint32_t loot_reset_serial = 0; // /loot reset: the window goes back to its default place and size
    std::map<std::uint32_t, std::int64_t> stats; // OP_ServerAuthStats keys 1..0x83, as they came, never cleared

private:
    void activate();
    void update_flags();
    void character_sets(const CharacterSets& sets);
    void server_stats(const std::vector<std::pair<std::uint32_t, std::int64_t>>& v);
    void zone_server_changed();
    void send_cauth();
    void ensure_loot_rules();
    void run_scribe(const HailstoneItemSlot& slot, std::uint8_t target_class);
    bool zone_packet(HailstonePacket& p, std::int64_t now);

    bool for_this_server() const;

    bool detected_ = false;
    bool server_ack_ = false;            // 0x140f from the server (0x10358f34)
    bool character_sets_seen_ = false;   // the login CRC rewrite waits for them
    std::uint32_t player_mask_ = 0;      // OP_ServerAuthStats key 1
    std::map<std::string, std::uint32_t> character_masks_; // OP_SendCharacterSets, by lower-case name
    std::vector<PetListEntry> pets_;     // OP_PetList
    bool have_pet_list_ = false;
    bool report_sent_ = false;           // our 0x140f this zone (re-armed by the next zone)
    std::uint8_t cast_class_ = 0;        // the Heartbeat's cast spoof, 0 none
    DisciplineTimers disc_timers_;
    std::uint32_t combat_now_ = 0;       // the doCombatAbility detour's time, taken before the original runs
    std::int64_t zone_in_ms_ = 0;        // for the "non multiclass server" warning
    bool warned_ = false;
    bool auth_sent_ = false;             // OP_CAuth sent this zone and not echoed yet
    std::int64_t auth_ms_ = 0;
    // The scribe path's state: g_pendingSpellBookShow, g_spellBookOpenedByScribe, the pending replay
    // (g_pendingScribeThis as the slot, its target class, g_scribeEndTick) and g_scribeInProgress, which makes
    // GetSpellLevelNeeded answer as for a scroll on the cursor while the original runs.
    struct PendingScribe {
        HailstoneItemSlot slot{};
        std::uint8_t target_class = 0;
        std::int64_t due_ms = 0;
    };
    bool show_book_pending_ = false;
    bool book_opened_by_scribe_ = false;
    std::optional<PendingScribe> scribe_;
    bool scribe_in_progress_ = false;
    std::string loot_rules_path_; // the character's itemrules.ini the rules were read from
};

Nms& plugin();

// ---- the host, in C++ (nms_plugin.cpp) ----
std::int64_t now_ms();
// The player as the client knows it; size 0 when it cannot be read.
HailstonePlayer player();
bool player_spawned(const HailstonePlayer& p); // its spawn is in the zone (the original's pinstLocalPlayer)
bool in_zone(const HailstonePlayer& p);        // in a zone (the connection's InZone or Camping)
std::optional<HailstoneSpell> spell(std::uint32_t id);
std::optional<HailstoneSpawn> spawn(std::uint32_t id);
std::optional<HailstoneItem> item(const HailstoneItemSlot& slot);
// A line in the chat window in one of the game's chat colours (0 the default).
void chat(const std::string& text, std::uint32_t color = 0);
void command_line(const std::string& line);
void send(const Packet& p, int stream = HAILSTONE_STREAM_ZONE);
std::string zone_name(std::uint32_t zone_id);
void log_info(const std::string& text);

} // namespace nms
