-- Autoplay cooldowns (this server's own window, nms.cooldowns: plugins/nms/nms_ui.cpp): a borderless frame with
-- each active auto-skill's name, the time left on the right and a bar that empties as it comes back, green "Ready"
-- when it is. Shown per the NMS setting nms.ShowCooldowns (as the classic window is); its X and its menu's Hide
-- clear it.
-- The patch's ">>" (pop out into an OS window) is left out: it does nothing in this client.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local hud = dofile("lib/hud.lua")
local T = hud.T
local C = T.color
local TITLE = "Autoplay Cooldowns"
local READY = C.pet -- (the palette's green)
local ROW, PAD, HEAD = 30, 8, 16

ui.window(TITLE, function()
  if ui.GetSetting("nms.ShowCooldowns", "0") ~= "1" then return end
  local rows = nms.cooldowns()
  if not rows then return end
  hud.Window(nil, TITLE, {
    shown = function() return true end,
    size = function() return 220, HEAD + PAD * 2 + math.max(#rows, 1) * ROW end,
    menu = function()
      if ui.MenuItem("Hide") then ui.SetSetting("nms.ShowCooldowns", "0") end
    end,
    onClose = function() ui.SetSetting("nms.ShowCooldowns", "0") end,
  }, function(x, y, w, h, s)
    T.Text(x + PAD * s, y + 5 * s, "COOLDOWNS", C.faint, T.font.tiny)
    local ry = y + (HEAD + PAD - 2) * s
    if #rows == 0 then
      T.Text(x + PAD * s, ry + 4 * s, "No active auto-skills", C.faint, T.font.small)
      return
    end
    local _, th = T.TextSize("Ag", T.font.small)
    for i, r in ipairs(rows) do
      local y0 = ry + (i - 1) * ROW * s
      local x0, x1 = x + PAD * s, x + w - PAD * s
      local ready = r[2] <= 0
      T.Text(x0, y0, T.Fit(r[1], x1 - x0 - 50 * s, T.font.small), C.text, T.font.small)
      T.Text(x1, y0, ready and "Ready" or string.format("%.1fs", r[2]), ready and READY or C.muted, T.font.small, "right")
      local by = y0 + th + 3 * s
      T.Bar("nmscd." .. r[1], x0, by, x1, by + 6 * s, s, ready and 1 or (r[3] or 0), ready and READY or C.cast,
        { noTrail = true })
    end
  end)
end)
