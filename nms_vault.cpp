// The shipped NMS patch's Dimensional Pocket Vault: its VAULTDATA parser (0x10199a20, 0x1019bed0). See
// nms_vault.h.

#include "nms_vault.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace nms {

namespace {

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t i = s.find(sep, at);
        if (i == std::string_view::npos) {
            out.emplace_back(s.substr(at));
            return out;
        }
        out.emplace_back(s.substr(at, i - at));
        at = i + 1;
    }
}

std::int32_t atoi_of(const std::string& s) { return static_cast<std::int32_t>(std::atoi(s.c_str())); }
std::int32_t strtol_of(const std::string& s) { return static_cast<std::int32_t>(std::strtol(s.c_str(), nullptr, 10)); }

} // namespace

std::vector<VaultBagItem> parse_vault_contents(std::string_view text) {
    std::vector<VaultBagItem> out;
    if (text.empty()) return out;
    for (const std::string& part : split(text, '~')) {
        if (part.empty()) continue;
        // Four ':' or the item is skipped; the name is the rest after the fourth.
        std::array<std::size_t, 4> colon{};
        std::size_t at = 0;
        bool ok = true;
        for (std::size_t k = 0; k < 4; ++k) {
            colon[k] = part.find(':', at);
            if (colon[k] == std::string::npos) {
                ok = false;
                break;
            }
            at = colon[k] + 1;
        }
        if (!ok) continue;
        VaultBagItem it;
        it.item_id = std::atoi(part.c_str());
        it.quantity = std::atoi(part.c_str() + colon[0] + 1);
        it.icon = std::atoi(part.c_str() + colon[1] + 1);
        it.bag_slot = std::atoi(part.c_str() + colon[2] + 1);
        it.name = part.substr(colon[3] + 1);
        out.push_back(std::move(it));
    }
    return out;
}

const char* vault_page_label(int page) {
    static const char* const kLabels[] = {"Bag 1",    "Bag 2",    "Bag 3",      "Bag 4", "Bag 5",
                                          "Bag 6",    "Clicky 1", "Clicky 2",   "Proc Locker"};
    return page >= 1 && page <= 9 ? kLabels[page - 1] : "";
}

const VaultSlot* Vault::at(std::int32_t slot) const {
    for (const auto& s : slots_)
        if (s.slot == slot) return &s;
    return nullptr;
}

void Vault::refresh_bag(const VaultSlot& s) {
    // An open bag window of that slot takes the new contents.
    if (auto it = bags_.find(s.slot); it != bags_.end()) it->second.contents = s.contents;
}

void Vault::toggle_bag(const VaultSlot& s) {
    // The right click on a bag slot (0x1019a420): a closed or missing window opens with the slot's name, number,
    // size and contents; an open one closes.
    auto it = bags_.find(s.slot);
    if (it == bags_.end() || !it->second.open) {
        BagWindow& b = bags_[s.slot];
        b.open = true;
        b.name = s.name;
        b.slot = s.slot;
        b.size = s.bag_size;
        b.contents = s.contents;
    } else {
        it->second.open = false;
    }
}

void Vault::close_bag(std::int32_t slot) {
    if (auto it = bags_.find(slot); it != bags_.end()) it->second.open = false;
}

bool Vault::on_chat(std::string_view line) {
    const std::size_t at = line.find("VAULTDATA|");
    if (at == std::string_view::npos) return false;
    const std::string_view p = line.substr(at);
    if (p.starts_with("VAULTDATA|CLEAR")) {
        exists_ = true;
        slots_.clear();
    } else if (p.starts_with("VAULTDATA|ADD|")) {
        exists_ = true;
        const auto f = split(p.substr(14), '|');
        if (f.size() <= 5) return true;
        VaultSlot s;
        s.key = atoi_of(f[0]);
        s.slot = atoi_of(f[1]);
        s.item_id = atoi_of(f[2]);
        s.unknown3 = atoi_of(f[3]);
        s.icon = atoi_of(f[4]);
        s.name = f[5];
        if (f.size() >= 7) s.contents_text = f[6];
        for (std::size_t k = 0; k < 6; ++k)
            if (f.size() >= 8 + k) s.augments[k] = strtol_of(f[7 + k]);
        if (f.size() >= 14) s.flag13 = atoi_of(f[13]) != 0;
        if (f.size() >= 15) s.bag_size = atoi_of(f[14]);
        s.contents = parse_vault_contents(s.contents_text);
        auto it = std::find_if(slots_.begin(), slots_.end(),
                               [&](const VaultSlot& e) { return e.key == s.key && e.slot == s.slot; });
        if (it != slots_.end()) *it = s;
        else slots_.push_back(s);
        refresh_bag(s);
    } else if (p.starts_with("VAULTDATA|NEST|")) {
        if (!exists_) return true;
        const std::string_view rest = p.substr(15);
        const std::size_t a = rest.find('|');
        if (a == std::string_view::npos) return true;
        const std::size_t b = rest.find('|', a + 1);
        if (b == std::string_view::npos) return true;
        const std::int32_t key = atoi_of(std::string(rest.substr(0, a)));
        const std::int32_t slot = atoi_of(std::string(rest.substr(a + 1, b - a - 1)));
        const std::string_view text = rest.substr(b + 1);
        if (text.empty()) return true;
        for (auto& s : slots_) {
            if (s.key != key || s.slot != slot) continue;
            if (!s.contents_text.empty()) s.contents_text += '~';
            s.contents_text += text;
            s.contents = parse_vault_contents(s.contents_text);
            refresh_bag(s);
            break;
        }
    } else if (p.starts_with("VAULTDATA|OPEN|")) {
        exists_ = true;
        page_ = std::max(1, std::atoi(std::string(p.substr(15)).c_str()));
        shown_ = true;
    }
    return true;
}

} // namespace nms
