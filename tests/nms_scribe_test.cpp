// The NMS plugin's scribe path (NMS_BeginScribeSpoof) against the mock EQEmu, the plugin loaded as a client loads
// it. Moved from the client's tests/item_use_test.cpp with the plugin.

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>

#include "mock_server/harness.h"
#include "net/item_packets.h"
#include "game/server_ext_chain.h"
#include "plugin/native_plugins.h"
#include "scratch_dir.h"

using namespace nmsc;
using namespace nmsc::net;
using namespace nmsc::test;

namespace {

std::uint32_t u32_at(const AppPacket& p, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, p.data.data() + at, 4);
    return v;
}

const AppPacket* last_of(const std::vector<AppPacket>& log, Op op) {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (from_wire(it->opcode) == op) return &*it;
    return nullptr;
}

MockConfig with_items(std::vector<Item> extra) {
    MockConfig c = MockConfig::standard();
    auto& inv = c.characters[0].inventory;
    for (auto& i : extra) inv.push_back(std::move(i));
    return c;
}

} // namespace

TEST_CASE("NMS plugin's scribe path: a tome at once, a scroll after the book, the sit and 100 ms") {
    Item tome = make_item(2001, "Tome of Kick", 25);
    tome.item_type = 20;
    tome.classes = 1u << 0;
    Item scroll = make_item(2002, "Spell: Minor Healing", 26);
    scroll.item_type = 20;
    scroll.classes = 1u << 1;
    // The NMS plugin (plugins/nms), loaded as a client loads it, on the connection's hooks and extension chain.
    ScratchDir scratch("nms_scribe_test");
    std::filesystem::create_directories(scratch.path() / "plugins");
    std::filesystem::copy_file(NMSC_NMS_PLUGIN, scratch.path() / "plugins" / std::filesystem::path(NMSC_NMS_PLUGIN).filename());
    plugin::Hooks hooks;
    plugin::Policy policy;
    policy.set(plugin::Rules{});
    auto chain = std::make_shared<game::ExtensionChain>();
    ConnectionOptions options = quick_options();
    options.hooks = &hooks;
    options.plugins = chain;
    MockConfig config = with_items({tome, scroll});
    config.characters[0].class_ = 2; // a cleric: /book opens the book
    Rig rig(std::move(config), std::move(options));
    auto& conn = rig.conn;
    plugin::NativeEnv ne;
    ne.hooks = &hooks;
    ne.policy = &policy;
    ne.run = [&conn](const std::string& line) { conn.send_command(line); };
    ne.send = [&conn](plugin::Stream, const AppPacket& p) { conn.send_zone_packet(p); };
    ne.extensions = chain.get();
    ne.connection = [&conn] { return &conn; };
    ne.dirs = {scratch.path() / "plugins"};
    ne.cache_dir = scratch.path() / "cache";
    ne.config_dir = scratch.path() / "config";
    plugin::NativePlugins plugins(ne);
    REQUIRE(plugins.load("nms"));
    rig.on_step = [&hooks] { hooks.pulse(); };
    REQUIRE(rig.to_zone());
    AppPacket ack(0x140f); // the server knows the patch: the plugin's NMS server
    hooks.packet(plugin::Stream::Zone, plugin::Direction::In, ack);
    REQUIRE(conn.server_ext().wide_item_links(conn));

    conn.right_click_item(ItemSlot::possessions(25));
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1; }));
    conn.receive_zone_packet_for_test(encode(ItemVerifyReply{ItemSlot::possessions(25), 0, 0}));
    CHECK_FALSE(conn.sitting());

    // The scroll: nothing goes at once; the next frame sits the player down, the click follows 100 ms later.
    const auto clicked = GameConnection::now_ms();
    conn.right_click_item(ItemSlot::possessions(26));
    CHECK(MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 1);
    REQUIRE(conn.take_window_toggle() == std::optional<std::string>("spellbook")); // /book; the app opens it
    REQUIRE(conn.open_spell_book());
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, Op::ItemVerifyRequest) == 2; }));
    CHECK(GameConnection::now_ms() - clicked >= 100); // the patch's SCRIBE_DURATION_MS
    CHECK(conn.sitting());
    const AppPacket* req = last_of(rig.server.zone_log, Op::ItemVerifyRequest);
    CHECK(u32_at(*req, 4) == (26u | 0xffffu << 16));

    // Standing up closes the book the scribe opened.
    REQUIRE(conn.spell_book_open());
    conn.send_command("/stand");
    rig.run_for(std::chrono::milliseconds(20));
    CHECK_FALSE(conn.sitting());
    CHECK_FALSE(conn.spell_book_open());
    plugins.unload_all();
}
