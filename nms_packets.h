#pragma once

// The NMS server's own packets (NMS/dinput8.dll and the NMS server's common/eq_packet_structs.h).
// The NMS opcodes are not in stock EQEmu's patch_RoF2.conf, so the plugin keeps them here by wire value. Decoders
// take the bytes OnPacket hands the plugin; encoders give a Packet for send_packet (and, in the server direction,
// for the tests).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "nms_rules.h"

namespace nms {

struct Packet {
    std::uint16_t opcode = 0;
    std::vector<std::byte> data;
};

using Bytes = std::span<const std::byte>;

// Wire opcodes: the NMS server's patch_RoF2.conf #CUSTOM block, and the stock ones the plugin watches.
namespace op {
inline constexpr std::uint16_t ServerAuthStats = 0x1338, SkillTimers = 0x1339, MulticlassCharSelect = 0x1340,
                               PetList = 0x1341, WaypointList = 0x1402, WaypointRequest = 0x1403,
                               CharacterSetRequest = 0x1404, SendCharacterSets = 0x1405, PersonalLootList = 0x1410,
                               PersonalLootAction = 0x1411, PersonalLootFilters = 0x1412,
                               PersonalLootSettings = 0x1413, PersonalLootHistory = 0x1414, CAuth = 0x7777;
// The patch's report and the server's acknowledgement (both directions); not in the server's conf either.
inline constexpr std::uint16_t ClientReport = 0x140f;
// Stock RoF2.
inline constexpr std::uint16_t World_Client_CRC1 = 0x0f13, World_Client_CRC2 = 0x4b8d, ZoneServerInfo = 0x4c44,
                               DisciplineTimer = 0x6989, Weblink = 0x6f4b;
} // namespace op

// ---- 0x140f, both directions ----
// Client -> zone (0x100eddc0, once after the class mask arrives): 34 bytes on the wire,
// {u16 0x140f; u32 0x5ab3e769; u32 flags; 24 zero bytes}. From the server (any payload) it says the server knows
// the patch, which mutes the "non multiclass server" warning; the patch swallows it.
inline constexpr std::uint32_t kReportMagic = 0x5ab3e769;
inline constexpr std::uint32_t kReportLauncher = 1;     // started by the NMS launcher (nms_launch.dat)
inline constexpr std::uint32_t kReportChannelT = 2;     // the DLL's channel letter is 'T' (the shipped one is 'L')
inline constexpr std::uint32_t kReportLargeAddress = 4; // the process has more than 2 GB of address space
Packet encode_client_report(std::uint32_t flags);

// ---- OP_CAuth 0x7777, client -> zone (0x100ed9f0) ----
// 262 bytes on the wire: {u16 0x7777; u8 hash[8]; 248 zero bytes; u32 0}, hash = (u64)(mask * spawn id) with each
// byte XORed with byte i % 4 of the server's Custom:ServerAuthKey. The server XORs all 256 bytes and compares the
// first 8 (zone/client_packet.cpp Handle_OP_CAuth).
Packet encode_cauth(std::uint32_t class_mask, std::uint32_t spawn_id, std::uint32_t key);

// ---- the login CRC rewrite (UdpConnection::SendMessage detour) ----
// The patch replaces the first 8 bytes of OP_World_Client_CRC1 (0x0f13) with the first network adapter's MAC and of
// CRC2 (0x4b8d) with the C:\ volume serial and 0, or, when its VM probe says VM, with 999999999999 and 99999999.
// This client never reads or sends real hardware identifiers: `Fixed` sends the patch's VM values, `None` leaves
// the packets as the stock client builds them (nms.ini hardware_id = fixed | none).
enum class HardwareIdMode : std::uint8_t { Fixed, None };
inline constexpr std::uint64_t kVmMac = 999999999999ull;
inline constexpr std::uint64_t kVmVolumeSerial = 99999999ull;
std::optional<HardwareIdMode> parse_hardware_id_mode(const std::string& s);
// Rewrites `data` in place when `opcode` is CRC1 or CRC2 and the mode is Fixed; returns whether it changed it.
bool apply_hardware_id(std::uint16_t opcode, std::span<std::byte> data, HardwareIdMode mode);

// ---- OP_ServerAuthStats 0x1338, zone -> client: {u32 count; count x {u32 key; u64 value}} ----
// nullopt unless the size is exactly 4 + 12 x count (the label parser, 0x10153300).
std::optional<std::vector<std::pair<std::uint32_t, std::int64_t>>> decode_server_auth_stats(Bytes p);
Packet encode_server_auth_stats(const std::vector<std::pair<std::uint32_t, std::int64_t>>& v); // for tests

// ---- OP_SendCharacterSets 0x1405, world -> client, before each OP_SendCharInfo ----
// OP_SendCharInfo lists only the characters of the selected set; this lists them all with their class masks.
struct CharacterSet {
    std::uint32_t id = 0;
    std::string name;
};
struct CharacterSetMember {
    std::uint32_t character_id = 0;
    std::string name;
    std::uint32_t level = 0;
    std::uint32_t classes = 0; // multiclass bit mask (GestaltClasses data bucket)
    std::vector<std::uint32_t> sets;
};
struct CharacterSets {
    std::uint32_t selected_set = 0, default_set = 0;
    std::uint32_t max_character_sets = 0, max_character_slots = 0, eom_available = 0;
    std::uint32_t character_slot_cost = 0, character_set_cost = 0;
    std::uint32_t available_slot_unlocks = 0, available_set_unlocks = 0;
    std::vector<CharacterSet> sets;
    std::vector<CharacterSetMember> characters;
};
std::optional<CharacterSets> decode_character_sets(Bytes p);
Packet encode(const CharacterSets& v); // server direction, for tests

// ---- OP_PetList 0x1341, zone -> client ----
// u32 count, count x {u32 spawn id, u32 summoning class (EQ class id; 0 for charmed pets)}: the server's list of the
// player's pets, sent on every change and at zone-in (zone/pets.cpp Client::SendPetList; the NMS add-on's MultiPet,
// Release-NMS-Client eqgame_dll/multi_pet.{h,cpp}).
struct PetListEntry {
    std::uint32_t spawn_id = 0;
    std::uint32_t class_id = 0;
};
std::optional<std::vector<PetListEntry>> decode_pet_list(Bytes p);
Packet encode_pet_list(const std::vector<PetListEntry>& pets); // server direction, for tests

// ---- OP_SkillTimers 0x1339, zone -> client ----
// {u32 backstab, bash, kick, strike, frenzy, autoskill_bitmask} (+ 4 more bytes from EQEmu's 4 + sizeof).
std::optional<SkillTimers> decode_skill_timers(Bytes p);
Packet encode_skill_timers(const SkillTimers& v); // server direction, for tests

// ---- OP_DisciplineTimer, zone -> client: {u32 id, u32 seconds left, u32}; only the first two are read ----
std::optional<std::pair<std::uint32_t, std::uint32_t>> decode_discipline_timer(Bytes p);

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
std::optional<WaypointList> decode_waypoint_list(Bytes p);
Packet encode(const WaypointList& v); // server direction, for tests
// OP_WaypointRequest 0x1403, client -> zone (7 bytes): {i32 id; bool expedition, group, autoconfirm}.
struct WaypointRequest {
    std::int32_t id = 0;
    bool expedition = false, group = false, autoconfirm = false;
};
Packet encode(const WaypointRequest& v);

// ---- personal loot (Custom:Loot_Enabled, zone/loot_distribution.cpp) ----
// NPC corpses are created empty and every drop goes into a per-player triage list. OP_PersonalLootList
// (zone -> client): u32 flags (1 show, 2 camping), u32 count, count x 160-byte rows (PersonalLootRow_Struct);
// count 0 clears the list.
struct PersonalLootRow {
    std::uint32_t pending_id = 0, item_id = 0, icon = 0, quantity = 0, charges = 0;
    std::uint32_t sell_value = 0;   // copper for Sell (0 = would be destroyed)
    std::uint32_t seconds_left = 0; // until the server delivers it anyway
    std::uint8_t rule = 0, lore = 0, no_drop = 0, augmented = 0;
    std::string item_name, npc_name;
};
struct PersonalLootList {
    std::uint32_t flags = 0;
    std::vector<PersonalLootRow> rows;
};
inline constexpr std::size_t kPersonalLootRowSize = 160;
std::optional<PersonalLootList> decode_personal_loot_list(Bytes p);
Packet encode(const PersonalLootList& v); // server direction, for tests

// OP_PersonalLootAction (client -> zone, 16 bytes): action, pending id, item id, value (PersonalLootActionType).
inline constexpr std::uint32_t kLootActionKeep = 1, kLootActionSell = 3, kLootActionLootAll = 5,
                               kLootActionSellAll = 6, kLootActionRefresh = 7, kLootActionInspect = 9,
                               kLootActionSetDefer = 14, kLootActionSetReport = 15, kLootActionSetSellUnder = 16,
                               kLootActionGetSettings = 17, kLootActionInspectItem = 18, kLootActionHistory = 19,
                               kLootActionHello = 20;
Packet encode_personal_loot_action(std::uint32_t action, std::uint32_t pending_id = 0, std::uint32_t item_id = 0,
                                   std::uint32_t value = 0);

// OP_PersonalLootFilters (zone -> client): u32 count, count x 80 bytes {u32 item_id, icon, sell_value (per unit);
// u8 rule (1 keep, 3 never); 3 pad; char name[64]}. The patch keeps its own rules and ignores these.
struct LootFilterRow {
    std::uint32_t item_id = 0, icon = 0, sell_value = 0;
    std::uint8_t rule = 0;
    std::string name;
};
std::optional<std::vector<LootFilterRow>> decode_loot_filters(Bytes p);
Packet encode(const std::vector<LootFilterRow>& v); // server direction, for tests

// OP_PersonalLootSettings (zone -> client, 16 bytes).
struct LootSettings {
    std::uint32_t flags = 0;           // bit 0: personal loot enabled on this server
    std::uint32_t sell_under = 0;      // copper: unknown items worth less are sold by the server
    std::uint32_t timeout_seconds = 0; // undecided items are looted after this
    bool defer_in_combat = false, report_enabled = false;
};
std::optional<LootSettings> decode_loot_settings(Bytes p);
Packet encode(const LootSettings& v); // server direction, for tests

// OP_PersonalLootHistory (zone -> client): u32 count, count x 152 bytes {u32 item_id, icon, quantity, copper,
// age_seconds; u8 action (1 kept, 2 sold, 3 destroyed); 3 pad; char item_name[64], npc_name[64]}, newest last.
struct LootHistoryRow {
    std::uint32_t item_id = 0, icon = 0, quantity = 0, copper = 0, age_seconds = 0;
    std::uint8_t action = 0;
    std::string item_name, npc_name;
};
std::optional<std::vector<LootHistoryRow>> decode_loot_history(Bytes p);
Packet encode(const std::vector<LootHistoryRow>& v); // server direction, for tests

} // namespace nms
