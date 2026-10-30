-- Dimensional Vault: this server's vault (plugins/nms/nms_vault.h, opened by the server's VAULTDATA|OPEN line) in
-- this UI's dialog frame; the pages, slots and bag windows are lib/nmsvault.lua. The close box hides it.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local hud = dofile("lib/hud.lua")
local V = dofile("lib/nmsvault.lua")

ui.window("Dimensional Vault", function()
  local v = nms.vault()
  if not v then return end
  hud.Dialog("NmsVaultWnd", "Dimensional Vault", {
    shown = v.shown,
    keepTitle = true,
    size = function() return 600, 600 end,
    resizable = { 420, 300 },
    onClose = function() nms.vault_window("hide") end,
  }, function() V.Body(v) end)
  for _, b in ipairs(v.bags) do V.Bag(v, b) end
end)
