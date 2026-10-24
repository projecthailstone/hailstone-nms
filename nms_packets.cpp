#include "plugins/nms/nms_packets.h"

#include <stdexcept>

#include "core/byte_io.h"
#include "net/packet_io.h"

namespace nmsc::nms {

using net::Op;
using net::detail::decode_with;
using net::is_op;
using net::make_packet;

AppPacket encode_client_report(std::uint32_t flags) {
    ByteWriter w;
    w.le<std::uint32_t>(kReportMagic);
    w.le<std::uint32_t>(flags);
    w.zeros(24);
    return AppPacket(kOpClientReport, w.take());
}

AppPacket encode_cauth(std::uint32_t class_mask, std::uint32_t spawn_id, std::uint32_t key) {
    const std::uint64_t value = static_cast<std::uint64_t>(class_mask) * spawn_id;
    ByteWriter w;
    for (std::size_t i = 0; i < 8; ++i)
        w.le<std::uint8_t>(static_cast<std::uint8_t>(((value >> (8 * i)) ^ (key >> (8 * (i % 4)))) & 0xff));
    w.zeros(248);
    w.le<std::uint32_t>(0);
    return make_packet(Op::CAuth, std::move(w));
}

std::optional<HardwareIdMode> parse_hardware_id_mode(const std::string& s) {
    if (s == "fixed") return HardwareIdMode::Fixed;
    if (s == "none") return HardwareIdMode::None;
    return std::nullopt;
}

bool apply_hardware_id(AppPacket& p, HardwareIdMode mode) {
    if (mode != HardwareIdMode::Fixed || p.data.size() < 8) return false;
    std::uint64_t v;
    if (is_op(p, Op::World_Client_CRC1)) v = kVmMac;
    else if (is_op(p, Op::World_Client_CRC2)) v = kVmVolumeSerial;
    else return false;
    for (std::size_t i = 0; i < 8; ++i) p.data[i] = static_cast<std::byte>((v >> (8 * i)) & 0xff);
    return true;
}

std::optional<SkillTimers> decode_skill_timers(const AppPacket& p) {
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

AppPacket encode_skill_timers(const SkillTimers& v) {
    ByteWriter w;
    for (std::uint32_t x : {v.backstab, v.bash, v.kick, v.strike, v.frenzy, v.autoskill}) w.le(x);
    w.zeros(4);
    return make_packet(Op::SkillTimers, std::move(w));
}

std::optional<std::string> decode_weblink(const AppPacket& p) {
    if (p.data.empty()) return std::nullopt;
    std::string s;
    for (std::byte b : p.data) {
        if (b == std::byte{0}) break;
        s.push_back(static_cast<char>(b));
    }
    if (s.empty()) return std::nullopt;
    return s;
}

std::optional<WaypointList> decode_waypoint_list(const AppPacket& p) {
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

AppPacket encode(const WaypointList& v) {
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
    return make_packet(Op::WaypointList, std::move(w));
}

AppPacket encode(const WaypointRequest& v) {
    ByteWriter w;
    w.le(v.id);
    for (bool b : {v.expedition, v.group, v.autoconfirm}) w.le<std::uint8_t>(b ? 1 : 0);
    return make_packet(Op::WaypointRequest, std::move(w));
}

} // namespace nmsc::nms
