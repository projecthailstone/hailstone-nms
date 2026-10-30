-- Personal Loot: replaces this server's loot window (the NMS patch's, plugins/nms/nms_loot.h) in this UI's dialog
-- frame: the four tabs of lib/nmsloot.lua (Loot, Rules, History, Settings), resizable. It opens when loot needs a
-- decision and with /loot; the close box hides it. On a stock server nms.loot() is nil.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local hud = dofile("lib/hud.lua")
local L = dofile("lib/nmsloot.lua")

ui.window("Personal Loot", function()
  local st = nms.loot()
  if not st then return end
  local prefs = L.Prefs()
  if st.reset then -- /loot reset: the default place and size from the next time it appears
    ui.SetSetting("hud.pos.NmsLootWnd", "")
    ui.SetSetting("hud.size.NmsLootWnd", "")
  end
  hud.Dialog("NmsLootWnd", L.Title(st), {
    shown = st.visible,
    keepTitle = true,
    size = function() return 760, 380 end,
    resizable = { 520, 240 },
    place = function() return 80, 80 end,
    onClose = function() nms.loot_window("show", false) end,
  }, function() L.Body(st, prefs) end)
end)
