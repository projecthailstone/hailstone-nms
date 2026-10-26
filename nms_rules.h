#pragma once

// The NMS client patch (NMS/dinput8.dll, the MQ2-based "eqgame_dll" the NMS server ships with the game) as rules
// on top of the stock ports. rof2decomp analysed it statically: notes/nms-dinput8.md (§ numbers below are that
// note's). On an NMS server the patched client is "the original", so the NMS module (plugins/nms/nms_extension.h)
// applies these overrides only once the connection detects an NMS server (GameConnection::nms_mode(): the world
// sent OP_SendCharacterSets, or the zone OP_ServerAuthStats or 0x140f) and only where the patch does (most of them
// need a non-zero class mask, as the patch's do). Everything here is a pure function so the stock ports keep
// matching their vectors.
//
// The class mask: bit c-1 = class c (Warrior bit 0 .. Berserker bit 15). NMS characters stay class 8 (Bard) in the
// profile and spawn; the patch spoofs the class only around particular calls.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/server_ext.h"

namespace nmsc::net { struct WhoResponse; }

namespace nmsc::nms {

inline constexpr std::uint32_t kBard = 8;
// FUN_10109d80 / FUN_10167bc0 / FUN_1011c6c0 and the IsSpellcaster detours: classes 2,3,4,5,6,8 and 10..15.
inline constexpr std::uint32_t kCasterMask = 0x7ebe;
// CSkillMgr::IsAvailable / HasSkill: Tracking needs Ranger, Druid or Bard.
inline constexpr std::uint32_t kTrackingMask = 0xa8;
inline constexpr std::int32_t kSkillTracking = 53;

inline bool has_class(std::uint32_t mask, std::uint32_t c) { return c >= 1 && c <= 16 && (mask >> (c - 1)) & 1u; }
inline std::uint32_t class_bit(std::uint32_t c) { return c >= 1 && c <= 32 ? 1u << (c - 1) : 0u; }

// The spell's level byte for a class: the trampoline of EQ_Spell::GetSpellLevelNeeded (orig(c)).
using LevelOf = std::function<std::uint8_t(std::uint32_t class_id)>;

// The lowest orig(c) over the mask's classes 1..16; 255 when the mask has none.
std::uint8_t mask_min_level(const LevelOf& orig, std::uint32_t mask);

// ---- EQ_Spell::GetSpellLevelNeeded @0x4af700, detour 0x10109030 (§3.2) ----
// `arg` the class asked about, `visible` the spawn's class (8 for NMS characters), `mask` the class mask, `player`
// whether a local player exists, `scribe` g_scribeInProgress or the cursor holding a type-20 scroll of this spell.
//   not scribing: mask 0 / no player -> orig(arg); arg == visible or a mask class -> the mask minimum; else 255.
//   scribing: 1 when the visible class (through the mask) or arg itself can use it at all, else 255.
std::uint8_t spell_level_needed(const LevelOf& orig, std::uint32_t arg, std::uint32_t visible, std::uint32_t mask,
                                bool player, bool scribe);

// ---- class rules (§3.1) ----
// EQ_Character::IsSpellcaster / _2 / _3 for the local character with a mask: any of classes 2..6, 8, 10..15.
inline bool is_spellcaster(std::uint32_t mask) { return (mask & kCasterMask) != 0; }
// EQ_Character::GetUsableClasses for the merchant filter (bArg1 == 0) with a mask: 0xffff when the item has no
// classes or one of the mask's; else the item's classes without the visible class (1 if that leaves none).
std::uint32_t usable_classes(std::uint32_t item_classes, std::uint32_t mask, std::uint32_t visible);
// CharacterZoneClient::BardCastBard detour: a spell whose Bard level byte (+0x24e) is 255 is never a song;
// otherwise the original decides.
inline bool bard_cast_bard_blocked(std::uint8_t raw_bard_level) { return raw_bard_level == 0xff; }
// Not the patch's (this client's own): the NMS server sends a multiclass caster's own 12205 "You begin casting %1."
// itself (zone/spells.cpp, Custom:MulticlassingEnabled, not for bard songs) through EQEmu's FilteredMessageCheck on
// FilterPCSpells. That maps the client's PCSpells setting 0 to show, 1 to hide (the client's "your own only") and
// anything else to group only, and group only drops the caster's own line when it is in no group and no raid.
// True when the line will not come.
inline bool own_begin_cast_dropped(std::int32_t pc_spells_setting, bool grouped, bool in_raid) {
    if (pc_spells_setting == 0) return false;
    if (pc_spells_setting == 1) return true;
    return !grouped && !in_raid;
}
// CharacterZoneClient::CanUseMemorizedSpellSlot detour: 12 gems from level 1.
inline bool can_use_memorized_spell_slot(std::int32_t gem) { return gem < 12; }

// ---- CInvSlot::HandleRButtonUp detour 0x1010a670 (§3.4): scribing from a right click ----
// Taken for item type 20 (scroll) or 31 (tome) while isToggleSpellBookOnScribeEnabled (1).
inline bool scribe_click(std::uint8_t item_type) { return item_type == 20 || item_type == 31; }
// The class the scribe spoof puts on the spawn and profile: the first class of the item's classes that is in the
// mask, else of the item's classes; 0 (no spoof) when the item has none.
std::uint8_t scribe_class(std::uint32_t item_classes, std::uint32_t mask);
// The instant tome path: type 31, or a type-20 item whose name holds Tome, tome, Skill, skill, Discipline or
// discipline (strstr, case as listed).
bool scribe_is_tome(std::uint8_t item_type, const std::string& name);
// The delayed path's wait between opening the book and the replayed click (SCRIBE_DURATION_MS).
inline constexpr std::int64_t kScribeDelayMs = 100;

// The Heartbeat's cast spoof (FUN_1016b670, §3.3): while a spell whose Bard level is 255 is being cast, the visible
// class is the first mask class (low bit first) with a level 1..254 for it; 0 when none (no spoof).
std::uint32_t cast_spoof_class(const LevelOf& orig, std::uint32_t mask);

// ---- skills (§3.8) ----
// The gate both detours run before spoofing: false for a skill the profile marks -1 (`profile_skill`), and for
// Tracking without Ranger, Druid or Bard in a non-zero mask; nullopt = go on.
std::optional<bool> skill_gate(std::int32_t skill, std::uint32_t mask, std::int32_t profile_skill);

// The tracking window's skill check (game::tracking_allowed) under the patch: with a class mask, Tracking needs
// Ranger, Druid or Bard in it (HasSkill's gate above); then the skill, as stock. A zero mask (any other server)
// gives the stock rule.
bool tracking_allowed(std::uint8_t class_id, std::uint32_t class_mask, std::uint32_t tracking_skill);
// GetSpoofClassForSkill (FUN_10109350): the class the original then sees for `skill`, first match in the mask;
// 0 = no spoof (the visible class stays).
std::uint32_t skill_spoof_class(std::int32_t skill, std::uint32_t mask);

// ---- displays (§3.6, §3.7, §3.10) ----
// The abbreviations GetClassDesc uses (table 0x10338230): "War", "Clr", ... joined with '/'; "" for mask 0.
std::string class_abbrevs(std::uint32_t mask);
// The upper-case codes (GetClassThreeLetterCode / the /who table): "WAR", "CLR", ...; "UNK" outside 1..16.
std::string class_code(std::uint32_t class_id);
// CEverQuest::GetClassDesc detour (0x100ef3a0): ids >= 1000 a mask (id - 1000), 200..449 a character select
// slot (`slot_mask(slot)` its mask, `slot_class(slot)` its original class), < 1 or > 450 class 1, anything else
// the original (`desc`).
std::string class_desc(std::int32_t id, const std::function<std::string(std::int32_t)>& desc,
                       const std::function<std::uint32_t(std::int32_t)>& slot_mask,
                       const std::function<std::uint32_t(std::int32_t)>& slot_class);
// The Heartbeat's window title, "EverQuest - <Name> [War/Clr]", less its "EverQuest - " (this client puts the
// server's name there, client_window_title): "<Name> [War/Clr]", "<Name>" for mask 0.
std::string window_title(const std::string& name, std::uint32_t mask);
// dsp_chat: "Your song ends." is dropped with a non-zero mask that has no Bard.
inline bool hide_song_ends(std::uint32_t mask) { return mask != 0 && !has_class(mask, kBard); }
// The rest of the dsp_chat detour's drops (FUN_10119d80): the vault's data lines ("VAULTDATA|", which the vault
// window reads) and its text replies ("--- DIMENSIONAL POCKET VAULT ---", "Pages: ", the deposit hint, "Slot " lines
// with Withdraw / Deposit Cursor / <Empty>, the rule), the echoes of the patch's own commands ("You say, '" with
// vault_page / vault_deposit / vault_withdraw / vault_bank / vault_merchant / #autoskill), and "launching the welcome
// screen". (Its LootNScoot "[NMS] Not active looter" throttle and MQ2's /filter list are not ported.)
bool hide_patch_line(std::string_view text);
// The item display's "Level Needed: " line for a spell scroll (FUN_101507b0 / ScrubTooltipLevelNeeded):
// "Level Needed: CLR(1) WIZ(4)", one per mask class with a level byte other than 0 and 255, "Level Needed: CANNOT
// USE" when none; with mask 0 the visible class alone.
std::string level_needed_line(const LevelOf& orig, std::uint32_t mask, std::uint32_t visible);
// The merchant (col 7) and bazaar (col 8) Lvl columns for a spell scroll (§3.6): the minimum over the mask's
// classes; else the minimum over all 16 classes when one can use it; nullopt = "--".
std::optional<std::uint8_t> list_level(const LevelOf& orig, std::uint32_t mask);

// The reformatted /who (FUN_10198eb0, who_multiclass.cpp): "[Name] - [Level CLASSES (Race)] ZONE: zone * GM *",
// "[Name] - [ANONYMOUS]..." for 5024, " <ROLEPLAY>" for 5023, then the player count line.
std::vector<std::string> format_who(const net::WhoResponse& r);

// ---- the discipline timers >= 20 (§2a GetDisciplineReuseTimer / SetDisciplineReuseTimer / doCombatAbility) ----
// The stock array holds timer ids 0..19; the patch keeps ids >= 20 in a map (0 = ready), ready times in seconds.
class DisciplineTimers {
public:
    static constexpr std::int32_t kFirst = 20;
    static bool handles(std::int32_t id) { return id >= kFirst; }
    std::uint32_t get(std::int32_t id) const;
    void set(std::int32_t id, std::uint32_t ready);
    bool ready(std::int32_t id, std::uint32_t now) const { return get(id) <= now; }
    std::uint32_t remaining(std::int32_t id, std::uint32_t now) const { return get(id) > now ? get(id) - now : 0; }
    void clear() { timers_.clear(); }

private:
    std::vector<std::pair<std::int32_t, std::uint32_t>> timers_;
};

// The vitals detours (§2c; EQCharacter1Hook in the source's MQ2Labels.cpp): Max_HP, Cur_HP, Max_Mana, Cur_Mana,
// Max_Endurance and Cur_Endurance return OP_ServerAuthStats key 5 / 2 / 6 / 3 / 7 / 4 when the patch's stat map has
// it, whatever the arguments and the EQ_Character; else the original. The 64-bit value as a signed compare: 0x7ffffffe
// and above give 0x7ffffffe, anything else its low 32 bits (so a negative value wraps). No lower clamp, no 10000000
// cap (the original's Max_HP / Cur_HP have one), nothing stored.
game::CharacterVitals server_vitals(const std::map<std::uint32_t, std::int64_t>& stats);

// NMSDiscTimers_ApplyBands (FUN_10107460): the CARecastTimerID the patch writes into a discipline (its table of 152
// at 0x10268a30, applied in game whenever spell 4514's is not 23); nullopt for the spells it leaves alone.
std::optional<std::int32_t> discipline_band(std::uint32_t spell_id);
// The doCombatAbility detour's refusal for a timer id >= 20 still running (WriteChatf at 0x10109820).
std::string discipline_not_ready_line(std::string_view name, std::uint32_t seconds);

// ---- OP_SkillTimers 0x1339 (§4.1) ----
// {u32 backstab, bash, kick, strike, frenzy, autoskill_bitmask}, the seconds left (EQEmu SkillTimers_Struct from
// PTimerList::GetRemainingTime; the server sends 4 more bytes).
struct SkillTimers {
    std::uint32_t backstab = 0, bash = 0, kick = 0, strike = 0, frenzy = 0, autoskill = 0;
};

// The patch's cooldown table (0x1035d634 seconds, 0x1035d65c the GetTickCount they were set at, 0x103381f0 the
// longest seen) over the ten auto-skills of its table at 0x10338000 (name, class mask +8, autoskill bit +0xc).
// FUN_100ecf40(slot, seconds): a positive value is stored with the time and raises the longest seen; anything else
// clears the slot. The 0x1339 case fills slots 0 backstab, 1 bash, 2 kick, 3 frenzy, 7 and 8 kick, 5, 6 and 9
// strike, and keeps the autoskill mask (0x1035d630); OP_Taunt 0x2703 sets slot 4 to 4 s.
struct AutoSkill {
    const char* name;
    std::uint32_t class_mask; // the classes that have it (bit c-1)
    std::uint32_t bit;        // its bit in the autoskill mask
};
inline constexpr std::array<AutoSkill, 10> kAutoSkills{{
    {"Backstab", 0x100, 0}, {"Bash", 0x17, 1}, {"Kick", 0xc049, 2}, {"Frenzy", 0x8000, 3}, {"Taunt", 0x1d, 4},
    {"Dragon Punch", 0x40, 5}, {"Eagle Strike", 0x40, 6}, {"Flying Kick", 0x40, 7}, {"Round Kick", 0x40, 8},
    {"Tiger Claw", 0x40, 9},
}};
class Cooldowns {
public:
    void set(std::size_t slot, float seconds, std::uint32_t now_ms);
    void on_skill_timers(const SkillTimers& t, std::uint32_t now_ms);
    // The seconds left (the stored value less the time since it was set, at least 0) and the bar's maximum (the
    // longest seen, 6 when not above 0).
    float remaining(std::size_t slot, std::uint32_t now_ms) const;
    float longest(std::size_t slot) const { return longest_[slot] > 0.0f ? longest_[slot] : 6.0f; }
    std::uint32_t autoskill_mask() const { return autoskill_; }

private:
    std::array<float, 10> seconds_{};
    std::array<std::uint32_t, 10> set_at_{};
    std::array<float, 10> longest_{10.0f, 6.0f, 6.0f, 10.0f, 4.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f}; // the DLL's data
    std::uint32_t autoskill_ = 0;
};

} // namespace nmsc::nms
