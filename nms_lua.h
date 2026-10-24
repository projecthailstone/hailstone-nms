#pragma once

// The NMS module's Lua windows (plugins/nms/nms_lua.cpp): the patch's own SIDL waypoint window (WaypointsWnd) and
// eq.NmsCooldowns for its "Autoplay Cooldowns" window (ui/<name>/nmswaypoints.lua, nmscooldowns.lua). They read the
// connection's NMS extension and show nothing on a stock server.

#include <memory>
#include <vector>

struct lua_State;

namespace nmsc::app {
struct EqHost;
class StockWindow;
} // namespace nmsc::app

namespace nmsc::nms {

void add_lua_windows(std::vector<std::unique_ptr<app::StockWindow>>& out);
// eq.NmsCooldowns, into the `eq` table on the top of the stack.
void register_lua_api(lua_State* L, app::EqHost& host);

} // namespace nmsc::nms
