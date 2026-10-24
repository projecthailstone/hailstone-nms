#pragma once

// The NMS client patch's own packets (NMS/dinput8.dll; rof2decomp notes/nms-dinput8.md §4). The patch's custom
// opcodes the NMS server knows are in opcodes_rof2.inc (generated from its patch_RoF2.conf); 0x140f is not in that
// file, so it is a constant (net/zone_packets.h, with OP_ServerAuthStats: the core detects an NMS server by them).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/packet.h"
#include "net/zone_packets.h"
#include "plugins/nms/nms_rules.h"

namespace nmsc::nms {

using net::AppPacket;

// ---- 0x140f, both directions ----
// Client -> zone (FUN_100eddc0, once after the class mask arrives): 34 bytes on the wire,
// {u16 0x140f; u32 0x5ab3e769; u32 flags; 24 zero bytes}. From the server (any payload) it says the server knows
// the patch, which mutes the "non multiclass server" warning; the patch swallows it.
inline constexpr std::uint16_t kOpClientReport = net::kOpNmsClientReport;
inline constexpr std::uint32_t kReportMagic = 0x5ab3e769;
inline constexpr std::uint32_t kReportLauncher = 1;     // started by the NMS launcher (nms_launch.dat)
inline constexpr std::uint32_t kReportChannelT = 2;     // the DLL's channel letter is 'T' (the shipped one is 'L')
inline constexpr std::uint32_t kReportLargeAddress = 4; // the process has more than 2 GB of address space
AppPacket encode_client_report(std::uint32_t flags);

// ---- OP_CAuth 0x7777, client -> zone (FUN_100ed9f0) ----
// 262 bytes on the wire: {u16 0x7777; u8 hash[8]; 248 zero bytes; u32 0}, hash = (u64)(mask * spawn id) with each
// byte XORed with byte i % 4 of the server's Custom:ServerAuthKey. The server XORs all 256 bytes and compares the
// first 8 (zone/client_packet.cpp Handle_OP_CAuth).
AppPacket encode_cauth(std::uint32_t class_mask, std::uint32_t spawn_id, std::uint32_t key);

// ---- the login CRC rewrite (UdpConnection::SendMessage detour, §4.2) ----
// The patch replaces the first 8 bytes of OP_World_Client_CRC1 (0x0f13) with the first network adapter's MAC and of
// CRC2 (0x4b8d) with the C:\ volume serial and 0, or, when its VM probe says VM, with 999999999999 and 99999999.
// This client never reads or sends real hardware identifiers: `Fixed` sends the patch's VM values, `None` leaves
// the packets as the stock client builds them (hailstone.ini nms.hardware_id = fixed | none).
enum class HardwareIdMode : std::uint8_t { Fixed, None };
inline constexpr std::uint64_t kVmMac = 999999999999ull;
inline constexpr std::uint64_t kVmVolumeSerial = 99999999ull;
std::optional<HardwareIdMode> parse_hardware_id_mode(const std::string& s);
// Rewrites `p` in place when it is CRC1 or CRC2 and the mode is Fixed; returns whether it changed it.
bool apply_hardware_id(AppPacket& p, HardwareIdMode mode);

// ---- OP_SkillTimers 0x1339, zone -> client ----
// {u32 backstab, bash, kick, strike, frenzy, autoskill_bitmask} (+ 4 more bytes from EQEmu's 4 + sizeof).
std::optional<SkillTimers> decode_skill_timers(const AppPacket& p);
AppPacket encode_skill_timers(const SkillTimers& v); // server direction, for tests

// ---- OP_Weblink 0x6f4b, zone -> client: the URL, NUL-terminated ----
std::optional<std::string> decode_weblink(const AppPacket& p);

// ---- OP_WaypointList 0x1402, zone -> client (zone/nms_waypoints.cpp SendWaypointList, packed) ----
// {bool group_enabled, expedition_enabled, group_selected, force_show, autoconfirm_selected; u32 count;
//  count x {i32 category, i32 id, u8 enabled, char name[64]}}.
struct WaypointEntry {
    std::int32_t category = 0, id = 0;
    bool enabled = false;
    std::string name;
};
struct WaypointList {
    bool group_enabled = false, expedition_enabled = false, group_selected = false, force_show = false,
         autoconfirm_selected = false;
    std::vector<WaypointEntry> entries;
};
std::optional<WaypointList> decode_waypoint_list(const AppPacket& p);
AppPacket encode(const WaypointList& v); // server direction, for tests
// OP_WaypointRequest 0x1403, client -> zone (7 bytes): {i32 id; bool expedition, group, autoconfirm}.
struct WaypointRequest {
    std::int32_t id = 0;
    bool expedition = false, group = false, autoconfirm = false;
};
AppPacket encode(const WaypointRequest& v);

} // namespace nmsc::nms
