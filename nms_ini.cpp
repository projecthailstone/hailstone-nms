#include "nms_ini.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace nms {

namespace {

bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

} // namespace

bool Ini::load(const std::filesystem::path& path) {
    sections_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream text;
    text << f.rdbuf();
    parse(text.str());
    return true;
}

bool Ini::save(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << to_string();
        if (!f.flush()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
    return !ec;
}

void Ini::parse(std::string_view text) {
    sections_.clear();
    Section* current = nullptr;
    while (!text.empty()) {
        std::size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            std::size_t close = line.find(']');
            std::string name(trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1)));
            current = find(name);
            if (!current) current = &sections_.emplace_back(Section{std::move(name), {}});
            continue;
        }
        std::size_t eq = line.find('=');
        if (!current || eq == std::string_view::npos) continue;
        set(current->name, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
}

std::string Ini::to_string() const {
    std::string out;
    for (const Section& s : sections_) {
        if (!out.empty()) out += '\n';
        out += '[' + s.name + "]\n";
        for (const auto& [k, v] : s.entries) out += k + '=' + v + '\n';
    }
    return out;
}

std::optional<std::string> Ini::get(std::string_view section, std::string_view key) const {
    if (const Section* s = find(section))
        for (const auto& [k, v] : s->entries)
            if (iequal(k, key)) return v;
    return std::nullopt;
}

std::string Ini::get_string(std::string_view section, std::string_view key, std::string_view fallback) const {
    auto v = get(section, key);
    return v ? *v : std::string(fallback);
}

std::vector<std::string> Ini::keys(std::string_view section) const {
    std::vector<std::string> out;
    if (const Section* s = find(section))
        for (const auto& [k, v] : s->entries) out.push_back(k);
    return out;
}

void Ini::set(std::string_view section, std::string_view key, std::string_view value) {
    Section* s = find(section);
    if (!s) s = &sections_.emplace_back(Section{std::string(section), {}});
    for (auto& [k, v] : s->entries)
        if (iequal(k, key)) {
            v = value;
            return;
        }
    s->entries.emplace_back(std::string(key), std::string(value));
}

Ini::Section* Ini::find(std::string_view name) {
    for (Section& s : sections_)
        if (iequal(s.name, name)) return &s;
    return nullptr;
}

const Ini::Section* Ini::find(std::string_view name) const {
    for (const Section& s : sections_)
        if (iequal(s.name, name)) return &s;
    return nullptr;
}

} // namespace nms
