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

#include "game/server_ext.h"
#include "net/item_packets.h"
#include "plugins/nms/nms_packets.h"
#include "plugins/nms/nms_rules.h"
#include "plugins/nms/nms_waypoints.h"

namespace nmsc { class Config; }

namespace nmsc::nms {

struct NmsOptions {
    // NMS servers with Custom:ServerAuthStats move clients that do not answer
    // the OP_CAuth check to the Bazaar; the key is that server's
    // Custom:ServerAuthKey. 0 = do not answer (fine for GM accounts).
    std::uint32_t auth_key = 0;
    // The NMS patch's login CRC rewrite (plugins/nms/nms_packets.h): Fixed sends its VM values in place of the
    // eqgame.exe and SkillCaps.txt checksums on an NMS world; None sends the stock checksums. Never real hardware ids.
    HardwareIdMode hardware_id = HardwareIdMode::Fixed;
    // The flags of the patch's 0x140f report (plugins/nms/nms_packets.h): not launcher-started, channel 'L',
    // large-address aware, as the shipped eqgame.exe (LAA) under the shipped DLL reports when started directly.
    std::uint32_t report_flags = kReportLargeAddress;
};
// hailstone.ini: nms.auth_key, nms.hardware_id = fixed (default) | none.
NmsOptions read_options(const Config& config);

// The NMS loot window's place (/nmsloot).
struct LootWindowState {
    bool show = true;   // shown while items wait for a decision
    bool reset = false; // /nmsloot reset: back to its default place and size
};
// The NMS client patch's "NMS loot window" for the server's personal loot list (Custom:Loot_Enabled); returns
// the button pressed: {action, pending id} (net::kLootAction*).
std::optional<std::pair<std::uint32_t, std::uint32_t>> loot_window(LootWindowState& state,
                                                                   const net::PersonalLootList& list,
                                                                   ui::ItemIconCache* icons);

class NmsExtension final : public game::ServerExtension {
public:
    explicit NmsExtension(NmsOptions options = {}) : options_(options) {}
    const char* name() const override { return "nms"; }

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
    bool hide_chat_line(const net::GameConnection& c, const std::string& text) const override;
    std::optional<std::vector<std::string>> who_lines(const net::GameConnection& c,
                                                      const net::WhoResponse& r) const override;

    // ---- classes (nms_extension.cpp) ----
    std::uint32_t class_mask(const net::GameConnection& c) const override;
    std::uint32_t character_class_mask(const net::GameConnection& c, const std::string& name) const override;
    std::uint8_t cast_class(const net::GameConnection&) const override { return cast_class_; }
    std::optional<bool> is_spellcaster(const net::GameConnection& c) const override;
    std::optional<std::uint8_t> spell_level_needed(const net::GameConnection& c, const game::SpellData& spell,
                                                   std::uint32_t class_id) const override;
    bool song_blocked(const net::GameConnection& c, std::uint8_t raw_bard_level) const override;
    std::optional<bool> can_use_gem(const net::GameConnection& c, int gem) const override;
    bool can_use_all_items(const net::GameConnection& c) const override { return class_mask(c) != 0; }
    std::string class_desc(const net::GameConnection& c, std::int32_t id) const override;
    std::string guild_member_class(const net::GameConnection& c, std::uint32_t class_value) const override;
    std::string window_title(const net::GameConnection& c) const override;
    // The patch NOPs the class test (0x45ea0f, 75 10 -> 90 90): moving breaks Hide unless sneaking, any class.
    std::uint8_t hide_check_class(const net::GameConnection&, std::uint8_t) const override { return 9; }
    bool aa_any_level(const net::GameConnection&) const override { return true; }
    bool aa_train_all(const net::GameConnection&) const override { return true; }
    std::vector<std::uint8_t> max_vitals_classes(const net::GameConnection& c, std::uint8_t class_id) const override;
    const game::SkillRules* skill_rules() const override;

    // ---- the Lua windows' values (nms_labels.cpp) ----
    std::function<std::string(const game::SpellData&)> level_needed_line(const net::GameConnection& c) const override;
    std::optional<std::string> merchant_level(const net::GameConnection& c, const net::Item& item) const override;
    void label_text(const net::GameConnection& c, int type, std::string& text) const override;

    // ---- commands and windows (nms_ui.cpp) ----
    bool window_toggle(net::GameConnection& c, const std::string& toggle, game::ExtensionAppContext& app) override;
    void draw(net::GameConnection& c, game::ExtensionAppContext& app) override;
    // /nmsloot [show|open|hide|close|reset|help]: the NMS loot window; returns the lines to print.
    std::vector<std::string> loot_command(const std::string& args);
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
    // The patch's discipline timers for ids >= 20 (Get/SetDisciplineReuseTimer detours), ready in Unix seconds.
    const DisciplineTimers& discipline_timers() const { return disc_timers_; }
    const NmsOptions& options() const { return options_; }

private:
    bool packet(net::GameConnection& c, const net::AppPacket& p);
    void send_cauth(net::GameConnection& c);

    NmsOptions options_;
    bool report_sent_ = false;     // our 0x140f this zone (re-armed by the next zone)
    std::uint8_t cast_class_ = 0;  // the Heartbeat's cast spoof, 0 none
    SkillTimers skill_timers_;
    Cooldowns cooldowns_;
    DisciplineTimers disc_timers_;
    std::optional<WaypointList> waypoints_;
    std::uint32_t waypoint_serial_ = 0;
    std::int64_t zone_in_ms_ = 0;  // for the "non multiclass server" warning
    bool warned_ = false;
    bool auth_sent_ = false;       // OP_CAuth sent this zone and not echoed yet
    std::chrono::steady_clock::time_point auth_time_{};
    LootWindowState loot_window_;
    WaypointWindowState waypoint_window_;
};

} // namespace nmsc::nms
