#pragma once

// The plugin's ini files (nms.ini, itemrules.ini): [Sections] of key=value lines, in file order. Sections and keys
// compare case-insensitively. Saving writes a temporary file and renames it over the old one.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nms {

class Ini {
public:
    bool load(const std::filesystem::path& path); // false (and empty) when the file cannot be read
    bool save(const std::filesystem::path& path) const;
    void parse(std::string_view text);
    std::string to_string() const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string get_string(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    // The keys of a section in file order (empty when there is no such section).
    std::vector<std::string> keys(std::string_view section) const;
    void set(std::string_view section, std::string_view key, std::string_view value);

private:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> entries;
    };
    Section* find(std::string_view name);
    const Section* find(std::string_view name) const;
    std::vector<Section> sections_;
};

} // namespace nms
