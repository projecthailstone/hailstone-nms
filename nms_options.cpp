#include "nms_options.h"

#include <cctype>
#include <cstdlib>

#include "nms_ini.h"

namespace nms {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Options read_options(const std::vector<const Ini*>& layers, std::vector<std::string>* warnings) {
    Options o;
    for (const Ini* layer : layers) {
        if (!layer) continue;
        const Ini& ini = *layer;
        if (auto key = ini.get("NMS", "auth_key"))
            o.auth_key = static_cast<std::uint32_t>(std::strtoul(key->c_str(), nullptr, 10));
        if (auto hw = ini.get("NMS", "hardware_id")) {
            if (auto mode = parse_hardware_id_mode(lower(*hw))) o.hardware_id = *mode;
            else if (warnings) warnings->push_back("hardware_id: unknown value '" + *hw + "' (fixed | none) ignored");
        }
        if (auto x = ini.get("NMS", "xtarget_hide_pets")) {
            const std::string v = lower(*x);
            o.xtarget_hide_pets = !(v == "0" || v == "false" || v == "off" || v == "no");
        }
        if (auto v = ini.get("NMS", "skip_servers")) o.skip_servers = split_words(*v);
    }
    return o;
}

bool server_matches(std::string_view name, std::string_view world, const std::vector<std::string>& words) {
    const std::string n = lower(std::string(name)), w = lower(std::string(world));
    for (const std::string& word : words) {
        const std::string k = lower(word);
        if (!k.empty() && (n.find(k) != std::string::npos || w.find(k) != std::string::npos)) return true;
    }
    return false;
}

std::vector<std::string> split_words(std::string_view list) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= list.size()) {
        std::size_t end = list.find(',', at);
        if (end == std::string_view::npos) end = list.size();
        std::string_view part = list.substr(at, end - at);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        if (!part.empty()) out.emplace_back(part);
        at = end + 1;
    }
    return out;
}

} // namespace nms
