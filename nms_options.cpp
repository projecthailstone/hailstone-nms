#include "nms_options.h"

#include <cctype>
#include <cstdlib>
#include <format>

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
    }
    return o;
}

std::string fov_command(const std::string& args, float& max_camera_distance) {
    std::string a = args;
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    if (a.empty()) return std::format("Current Max Zoom Distance is: {:f}", max_camera_distance);
    float v = std::strtof(a.c_str(), nullptr);
    if (v < 10.0f || v > 150.0f)
        return "Usage: /fov <zoom_distance> (min 10, max 150. Default is usually around 53. Try /fov 90)";
    max_camera_distance = v;
    return std::format("Max zoom distance set to {:f}", v);
}

} // namespace nms
