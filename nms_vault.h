#pragma once

// The shipped NMS patch's "Dimensional Pocket Vault" (NMS/dinput8.dll; rof2decomp notes/nms-dinput8.md §4.3): a
// window over a server-side store of 83 slots, fed by chat lines the server sends and the patch's dsp_chat detour
// reads and hides (FUN_10119d80 -> FUN_10199a20), and driven by "#vault_*" say commands. The NMS-Release server has
// no vault (no #vault command, no VAULTDATA line), so only the client side is known; field meanings come from how
// the window uses them (FUN_1019a420 the window, FUN_1019b500 the bag windows, FUN_1019bc60 the contents tooltip).
//
// The lines (anywhere in a chat line, from "VAULTDATA|"):
//   VAULTDATA|CLEAR                     drops every slot
//   VAULTDATA|ADD|f0|f1|...|f14         a slot (at least 6 fields): f0 (?) and f1 (the slot, 1..83) key it (the same
//                                       pair replaces it); f2 item id; f3 (?); f4 icon; f5 name; f6 a bag's contents;
//                                       f7..f12 the six augment ids; f13 (?, a flag); f14 the bag's size (> 0: a bag)
//   VAULTDATA|NEST|f0|f1|text           more of slot (f0, f1)'s contents: f6 += "~" + text (no "~" when f6 is empty)
//   VAULTDATA|OPEN|page                 shows the window at page (at least 1)
// A bag's contents (f6): "~"-separated items "item:qty:icon:bag slot:name" (the bag slot 0-based; FUN_1019bed0).
//
// Commands (said, as HideDoCommand "/say ..."): #vault_page N (a page button), #vault_deposit S (a click with an item
// on the cursor), #vault_withdraw S [1] (a click; with Ctrl the trailing 1), #vault_bank, #vault_merchant, and in a
// bag's window #vault_deposit_bagitem_specific S I / #vault_withdraw_bagitem S I [1].

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc::nms {

struct VaultBagItem {
    std::int32_t item_id = 0, quantity = 0, icon = 0, bag_slot = 0; // bag_slot 0-based
    std::string name;
};

struct VaultSlot {
    std::int32_t key = 0;  // f0
    std::int32_t slot = 0; // f1: 1..83 (page p holds (p - 1) * 10 + 1 .. p * 10; page 9 holds 81..83)
    std::int32_t item_id = 0, unknown3 = 0, icon = 0;
    std::string name;
    std::string contents_text; // f6, as received (NEST lines extend it)
    std::array<std::int32_t, 6> augments{};
    bool flag13 = false;
    std::int32_t bag_size = 0;
    std::vector<VaultBagItem> contents; // f6 parsed
};

// "item:qty:icon:bag slot:name" items separated by "~"; an item without four ':' is skipped.
std::vector<VaultBagItem> parse_vault_contents(std::string_view text);

// The page buttons' labels: "Bag 1" .. "Bag 6", "Clicky 1", "Clicky 2", "Proc Locker".
const char* vault_page_label(int page);
// The first and last slot of a page (1..9).
inline int vault_page_first(int page) { return (page - 1) * 10 + 1; }
inline int vault_page_last(int page) { return page == 9 ? 83 : page * 10; }

class Vault {
public:
    // A chat line: true when it carried "VAULTDATA|" (read here; the patch hides it).
    bool on_chat(std::string_view line);

    bool shown() const { return shown_; }
    void show(bool s) { shown_ = s; }
    int page() const { return page_; }
    // The slot at f1 == slot (the window's lookup), null when empty.
    const VaultSlot* at(std::int32_t slot) const;
    const std::vector<VaultSlot>& slots() const { return slots_; }

    // A bag's own window (a right click on a bag slot toggles it): its contents follow later ADD / NEST lines.
    struct BagWindow {
        bool open = false;
        std::string name;
        std::int32_t slot = 0, size = 0;
        std::vector<VaultBagItem> contents;
    };
    void toggle_bag(const VaultSlot& s);
    void close_bag(std::int32_t slot);
    const std::map<std::int32_t, BagWindow>& bags() const { return bags_; }

private:
    void refresh_bag(const VaultSlot& s);

    bool exists_ = false; // the patch creates its vault object on the first CLEAR / ADD / OPEN
    bool shown_ = false;
    int page_ = 1;
    std::vector<VaultSlot> slots_;
    std::map<std::int32_t, BagWindow> bags_;
};

} // namespace nmsc::nms
