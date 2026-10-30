#pragma once

// The NMS plugin's settings (nms.ini) and the patch's /fov.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nms_packets.h"

namespace nms {

class Ini;

// The key the current NMS patch compiles into its OP_CAuth sender (NMS-Release eqgame_dll/eqgame.cpp
// FireCAuthHandshake: private_key = 352236586), the server's Custom:ServerAuthKey default (common/ruletypes.h).
inline constexpr std::uint32_t kPatchAuthKey = 352236586;

struct Options {
    // NMS servers with Custom:ServerAuthStats move clients that do not answer the OP_CAuth check to the Bazaar; the
    // key is that server's Custom:ServerAuthKey. The default is the current patch's own key (kPatchAuthKey); 0 = do
    // not answer (fine for GM accounts).
    std::uint32_t auth_key = kPatchAuthKey;
    // The NMS patch's login CRC rewrite (nms_packets.h): Fixed sends its VM values in place of the eqgame.exe and
    // SkillCaps.txt checksums on an NMS world; None sends the stock checksums. Never real hardware ids.
    HardwareIdMode hardware_id = HardwareIdMode::Fixed;
    // The flags of the patch's 0x140f report (nms_packets.h): not launcher-started, channel 'L', large-address
    // aware, as the shipped eqgame.exe (LAA) under the shipped DLL reports when started directly.
    std::uint32_t report_flags = kReportLargeAddress;
    // Hailstone's: the extended target window leaves out the player's own pets the server puts in its last slots
    // (xtarget_server_pet_slot); the Options window's "Hide My Pets in Extended Target", on by default.
    bool xtarget_hide_pets = true;
    // Servers it stands down on although they send the NMS packets: their own plugin's (Project Triune, a build of the
    // same patch: plugins/projecttriune). Matched against the saved server's and the world's names (server_matches).
    std::vector<std::string> skip_servers{"triune"};
};
// nms.ini [NMS]: auth_key, hardware_id = fixed (default) | none, xtarget_hide_pets = 1 (default) | 0,
// skip_servers = triune (default; a comma list). Each layer over
// the one before: the plugin's own (plugins/nms/nms.ini), the saved server's server.ini [NMS] (where builds before
// the plugin kept auth_key and hardware_id), the server's plugin-config/nms/nms.ini. Unknown values keep what was
// there and are named in `warnings`.
Options read_options(const std::vector<const Ini*>& layers, std::vector<std::string>* warnings = nullptr);

// The patch's /fov (and /camera) <10..150>: despite the name it sets the maximum camera zoom distance
// (__gfMaxZoomCameraDistance and __gfMaxCameraDistance, MQ2Commands.cpp CmdFov), not the field of view. Returns the
// line to print; `max_camera_distance` changes when the argument is in range.
// Whether the server is one of `words` (case-insensitive; each word somewhere in the saved server's name or the
// world's name, as server_info gives them). An empty word never matches.
bool server_matches(std::string_view name, std::string_view world, const std::vector<std::string>& words);
// "a, b ,c" -> {"a", "b", "c"} (empty parts dropped).
std::vector<std::string> split_words(std::string_view list);

std::string fov_command(const std::string& args, float& max_camera_distance);

} // namespace nms
