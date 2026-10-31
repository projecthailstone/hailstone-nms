#include "nms_packets.h"

#include <stdexcept>

#include "nmsc_byte_io.h"

namespace nms {

using nmsc::ByteReader;
using nmsc::ByteWriter;

namespace {

// A decoder's body over the bytes: nullopt when it reads past the end or finds a count that cannot fit.
template <typename F> auto decode_with(Bytes p, F&& body) -> std::optional<decltype(body(std::declval<ByteReader&>()))> {
    try {
        ByteReader r(p);
        return body(r);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    }
}

Packet packet(std::uint16_t opcode, ByteWriter&& w) { return Packet{opcode, w.take()}; }

// OP_SendCharacterSets: 11 u32s, then 64 sets of {u32 id; char name[64]}, then each character.
constexpr std::size_t kMaxCharacterSets = 64;
constexpr std::size_t kCharacterSetSize = 4 + 64;
constexpr std::size_t kCharacterSetListSize = 11 * 4 + kMaxCharacterSets * kCharacterSetSize; // 4396
constexpr std::size_t kCharacterEntrySize = 4 + 64 + 4 + 4 + kMaxCharacterSets * 4;          // 332

} // namespace

Packet encode_client_report(std::uint32_t flags) {
    ByteWriter w;
    w.le<std::uint32_t>(kReportMagic);
    w.le<std::uint32_t>(flags);
    w.zeros(24);
    return packet(op::ClientReport, std::move(w));
}

Packet encode_cauth(std::uint32_t class_mask, std::uint32_t spawn_id, std::uint32_t key) {
    const std::uint64_t value = static_cast<std::uint64_t>(class_mask) * spawn_id;
    ByteWriter w;
    for (std::size_t i = 0; i < 8; ++i)
        w.le<std::uint8_t>(static_cast<std::uint8_t>(((value >> (8 * i)) ^ (key >> (8 * (i % 4)))) & 0xff));
    w.zeros(248);
    w.le<std::uint32_t>(0);
    return packet(op::CAuth, std::move(w));
}

std::optional<HardwareIdMode> parse_hardware_id_mode(const std::string& s) {
    if (s == "fixed") return HardwareIdMode::Fixed;
    if (s == "none") return HardwareIdMode::None;
    return std::nullopt;
}

bool apply_hardware_id(std::uint16_t opcode, std::span<std::byte> data, HardwareIdMode mode) {
    if (mode != HardwareIdMode::Fixed || data.size() < 8) return false;
    std::uint64_t v;
    if (opcode == op::World_Client_CRC1) v = kVmMac;
    else if (opcode == op::World_Client_CRC2) v = kVmVolumeSerial;
    else return false;
    for (std::size_t i = 0; i < 8; ++i) data[i] = static_cast<std::byte>((v >> (8 * i)) & 0xff);
    return true;
}

std::optional<std::vector<std::pair<std::uint32_t, std::int64_t>>> decode_server_auth_stats(Bytes p) {
    if (p.size() < 4) return std::nullopt;
    return decode_with(p, [&p](ByteReader& r) {
        std::uint32_t count = r.le<std::uint32_t>();
        if (static_cast<std::uint64_t>(count) * 12 != p.size() - 4) throw std::length_error("auth stats: size");
        std::vector<std::pair<std::uint32_t, std::int64_t>> v;
        v.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t key = r.le<std::uint32_t>();
            v.emplace_back(key, static_cast<std::int64_t>(r.le<std::uint64_t>()));
        }
        return v;
    });
}

Packet encode_server_auth_stats(const std::vector<std::pair<std::uint32_t, std::int64_t>>& v) {
    ByteWriter w;
    w.le<std::uint32_t>(static_cast<std::uint32_t>(v.size()));
    for (const auto& [k, x] : v) {
        w.le(k);
        w.le(static_cast<std::uint64_t>(x));
    }
    return packet(op::ServerAuthStats, std::move(w));
}

std::optional<CharacterSets> decode_character_sets(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        CharacterSets v;
        v.selected_set = r.le<std::uint32_t>();
        v.default_set = r.le<std::uint32_t>();
        std::uint32_t set_count = r.le<std::uint32_t>();
        std::uint32_t char_count = r.le<std::uint32_t>();
        v.max_character_sets = r.le<std::uint32_t>();
        v.max_character_slots = r.le<std::uint32_t>();
        v.eom_available = r.le<std::uint32_t>();
        v.character_slot_cost = r.le<std::uint32_t>();
        v.character_set_cost = r.le<std::uint32_t>();
        v.available_slot_unlocks = r.le<std::uint32_t>();
        v.available_set_unlocks = r.le<std::uint32_t>();
        if (set_count > kMaxCharacterSets) throw std::out_of_range("set count");
        for (std::size_t i = 0; i < kMaxCharacterSets; ++i) {
            CharacterSet s;
            s.id = r.le<std::uint32_t>();
            s.name = r.fixed_string(64);
            if (i < set_count) v.sets.push_back(std::move(s));
        }
        if (r.position() != kCharacterSetListSize) throw std::out_of_range("set list size");
        if (char_count != r.remaining() / kCharacterEntrySize || r.remaining() % kCharacterEntrySize)
            throw std::out_of_range("character entries");
        for (std::uint32_t i = 0; i < char_count; ++i) {
            CharacterSetMember m;
            m.character_id = r.le<std::uint32_t>();
            m.name = r.fixed_string(64);
            m.level = r.le<std::uint32_t>();
            m.classes = r.le<std::uint32_t>();
            for (std::size_t j = 0; j < kMaxCharacterSets; ++j) {
                std::uint32_t set = r.le<std::uint32_t>();
                if (set) m.sets.push_back(set);
            }
            v.characters.push_back(std::move(m));
        }
        return v;
    });
}

Packet encode(const CharacterSets& v) {
    ByteWriter w;
    for (std::uint32_t x : {v.selected_set, v.default_set, static_cast<std::uint32_t>(v.sets.size()),
                            static_cast<std::uint32_t>(v.characters.size()), v.max_character_sets,
                            v.max_character_slots, v.eom_available, v.character_slot_cost, v.character_set_cost,
                            v.available_slot_unlocks, v.available_set_unlocks})
        w.le(x);
    for (std::size_t i = 0; i < kMaxCharacterSets; ++i) {
        const CharacterSet* s = i < v.sets.size() ? &v.sets[i] : nullptr;
        w.le<std::uint32_t>(s ? s->id : 0);
        w.fixed_string(s ? s->name : std::string(), 64);
    }
    for (const auto& m : v.characters) {
        w.le(m.character_id);
        w.fixed_string(m.name, 64);
        w.le(m.level);
        w.le(m.classes);
        for (std::size_t j = 0; j < kMaxCharacterSets; ++j) w.le<std::uint32_t>(j < m.sets.size() ? m.sets[j] : 0);
    }
    return packet(op::SendCharacterSets, std::move(w));
}

std::optional<std::vector<PetListEntry>> decode_pet_list(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        std::uint32_t count = r.le<std::uint32_t>();
        if (static_cast<std::size_t>(count) * 8 > r.remaining()) throw std::length_error("pet list: count");
        std::vector<PetListEntry> pets(count);
        for (auto& e : pets) {
            e.spawn_id = r.le<std::uint32_t>();
            e.class_id = r.le<std::uint32_t>();
        }
        return pets;
    });
}

Packet encode_pet_list(const std::vector<PetListEntry>& pets) {
    ByteWriter w;
    w.le<std::uint32_t>(static_cast<std::uint32_t>(pets.size()));
    for (const auto& e : pets) {
        w.le<std::uint32_t>(e.spawn_id);
        w.le<std::uint32_t>(e.class_id);
    }
    return packet(op::PetList, std::move(w));
}

std::optional<SkillTimers> decode_skill_timers(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        SkillTimers v;
        v.backstab = r.le<std::uint32_t>();
        v.bash = r.le<std::uint32_t>();
        v.kick = r.le<std::uint32_t>();
        v.strike = r.le<std::uint32_t>();
        v.frenzy = r.le<std::uint32_t>();
        v.autoskill = r.le<std::uint32_t>();
        return v;
    });
}

Packet encode_skill_timers(const SkillTimers& v) {
    ByteWriter w;
    for (std::uint32_t x : {v.backstab, v.bash, v.kick, v.strike, v.frenzy, v.autoskill}) w.le(x);
    w.zeros(4);
    return packet(op::SkillTimers, std::move(w));
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> decode_discipline_timer(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        const std::uint32_t id = r.le<std::uint32_t>();
        return std::pair<std::uint32_t, std::uint32_t>{id, r.le<std::uint32_t>()};
    });
}

std::optional<WaypointList> decode_waypoint_list(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        WaypointList v;
        v.group_enabled = r.le<std::uint8_t>() != 0;
        v.expedition_enabled = r.le<std::uint8_t>() != 0;
        v.group_selected = r.le<std::uint8_t>() != 0;
        v.force_show = r.le<std::uint8_t>() != 0;
        v.autoconfirm_selected = r.le<std::uint8_t>() != 0;
        std::uint32_t count = r.le<std::uint32_t>();
        if (static_cast<std::size_t>(count) * 73 > r.remaining()) throw std::length_error("waypoint list: count");
        for (std::uint32_t i = 0; i < count; ++i) {
            WaypointEntry e;
            e.category = r.le<std::int32_t>();
            e.id = r.le<std::int32_t>();
            e.enabled = r.le<std::uint8_t>() != 0;
            e.name = r.fixed_string(64);
            v.entries.push_back(std::move(e));
        }
        return v;
    });
}

Packet encode(const WaypointList& v) {
    ByteWriter w;
    for (bool b : {v.group_enabled, v.expedition_enabled, v.group_selected, v.force_show, v.autoconfirm_selected})
        w.le<std::uint8_t>(b ? 1 : 0);
    w.le<std::uint32_t>(static_cast<std::uint32_t>(v.entries.size()));
    for (const auto& e : v.entries) {
        w.le(e.category);
        w.le(e.id);
        w.le<std::uint8_t>(e.enabled ? 1 : 0);
        w.fixed_string(e.name, 64);
    }
    return packet(op::WaypointList, std::move(w));
}

Packet encode(const WaypointRequest& v) {
    ByteWriter w;
    w.le(v.id);
    for (bool b : {v.expedition, v.group, v.autoconfirm}) w.le<std::uint8_t>(b ? 1 : 0);
    return packet(op::WaypointRequest, std::move(w));
}

std::optional<PersonalLootList> decode_personal_loot_list(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        PersonalLootList v;
        v.flags = r.le<std::uint32_t>();
        std::uint32_t count = r.le<std::uint32_t>();
        if (count > r.remaining() / kPersonalLootRowSize) throw std::out_of_range("count exceeds payload");
        for (std::uint32_t i = 0; i < count; ++i) {
            PersonalLootRow row;
            row.pending_id = r.le<std::uint32_t>();
            row.item_id = r.le<std::uint32_t>();
            row.icon = r.le<std::uint32_t>();
            row.quantity = r.le<std::uint32_t>();
            row.charges = r.le<std::uint32_t>();
            row.sell_value = r.le<std::uint32_t>();
            row.seconds_left = r.le<std::uint32_t>();
            row.rule = r.le<std::uint8_t>();
            row.lore = r.le<std::uint8_t>();
            row.no_drop = r.le<std::uint8_t>();
            row.augmented = r.le<std::uint8_t>();
            row.item_name = r.fixed_string(64);
            row.npc_name = r.fixed_string(64);
            v.rows.push_back(std::move(row));
        }
        return v;
    });
}

Packet encode(const PersonalLootList& v) {
    ByteWriter w;
    w.le(v.flags);
    w.le(static_cast<std::uint32_t>(v.rows.size()));
    for (const auto& row : v.rows) {
        for (auto x : {row.pending_id, row.item_id, row.icon, row.quantity, row.charges, row.sell_value,
                       row.seconds_left})
            w.le(x);
        for (auto x : {row.rule, row.lore, row.no_drop, row.augmented}) w.le(x);
        w.fixed_string(row.item_name, 64);
        w.fixed_string(row.npc_name, 64);
    }
    return packet(op::PersonalLootList, std::move(w));
}

Packet encode_personal_loot_action(std::uint32_t action, std::uint32_t pending_id, std::uint32_t item_id,
                                   std::uint32_t value) {
    ByteWriter w;
    w.le(action);
    w.le(pending_id);
    w.le(item_id);
    w.le(value);
    return packet(op::PersonalLootAction, std::move(w));
}

std::optional<std::vector<LootFilterRow>> decode_loot_filters(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        std::vector<LootFilterRow> v;
        const std::uint32_t count = r.le<std::uint32_t>();
        if (count > r.remaining() / 80) throw std::length_error("loot filters: count");
        for (std::uint32_t i = 0; i < count; ++i) {
            LootFilterRow f;
            f.item_id = r.le<std::uint32_t>();
            f.icon = r.le<std::uint32_t>();
            f.sell_value = r.le<std::uint32_t>();
            f.rule = r.le<std::uint8_t>();
            r.skip(3);
            f.name = r.fixed_string(64);
            v.push_back(std::move(f));
        }
        return v;
    });
}

Packet encode(const std::vector<LootFilterRow>& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& f : v) {
        for (std::uint32_t x : {f.item_id, f.icon, f.sell_value}) w.le(x);
        w.le(f.rule);
        w.zeros(3);
        w.fixed_string(f.name, 64);
    }
    return packet(op::PersonalLootFilters, std::move(w));
}

std::optional<LootSettings> decode_loot_settings(Bytes p) {
    // OnSettings: shorter than the struct is ignored.
    if (p.size() < 16) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        LootSettings s;
        s.flags = r.le<std::uint32_t>();
        s.sell_under = r.le<std::uint32_t>();
        s.timeout_seconds = r.le<std::uint32_t>();
        s.defer_in_combat = r.le<std::uint8_t>() != 0;
        s.report_enabled = r.le<std::uint8_t>() != 0;
        return s;
    });
}

Packet encode(const LootSettings& s) {
    ByteWriter w;
    for (std::uint32_t x : {s.flags, s.sell_under, s.timeout_seconds}) w.le(x);
    w.le<std::uint8_t>(s.defer_in_combat ? 1 : 0);
    w.le<std::uint8_t>(s.report_enabled ? 1 : 0);
    w.zeros(2);
    return packet(op::PersonalLootSettings, std::move(w));
}

std::optional<std::vector<LootHistoryRow>> decode_loot_history(Bytes p) {
    return decode_with(p, [](ByteReader& r) {
        std::vector<LootHistoryRow> v;
        const std::uint32_t count = r.le<std::uint32_t>();
        if (count > r.remaining() / 152) throw std::length_error("loot history: count");
        for (std::uint32_t i = 0; i < count; ++i) {
            LootHistoryRow h;
            h.item_id = r.le<std::uint32_t>();
            h.icon = r.le<std::uint32_t>();
            h.quantity = r.le<std::uint32_t>();
            h.copper = r.le<std::uint32_t>();
            h.age_seconds = r.le<std::uint32_t>();
            h.action = r.le<std::uint8_t>();
            r.skip(3);
            h.item_name = r.fixed_string(64);
            h.npc_name = r.fixed_string(64);
            v.push_back(std::move(h));
        }
        return v;
    });
}

Packet encode(const std::vector<LootHistoryRow>& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& h : v) {
        for (std::uint32_t x : {h.item_id, h.icon, h.quantity, h.copper, h.age_seconds}) w.le(x);
        w.le(h.action);
        w.zeros(3);
        w.fixed_string(h.item_name, 64);
        w.fixed_string(h.npc_name, 64);
    }
    return packet(op::PersonalLootHistory, std::move(w));
}

} // namespace nms
