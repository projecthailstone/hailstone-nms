-- Multiclass Autoplay Console (gamepad pack): this server's console (nms.autoplay: plugins/nms/nms_ui.cpp), as
-- the Modern UI's: for each auto-skill the character's classes have, a toggle (the server's autoskill bit; A asks the
-- server to turn it on or off, "#autoskill <name> enable|disable") with its cooldown bar beside it, green "Ready" when
-- it is back; then Show Cooldown Window (nms.ShowCooldowns: its Autoplay Cooldowns, nmscooldowns.lua). Shown per
-- nms.ShowConsole (as the other packs) and toggled with /autoskill; B hides it.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local C = T.C
local NAME = "Autoplay Console"

local wasShown = false

ui.window("Multiclass Autoplay Console", function()
  if not eq.InGame() or not eq.Plugin("nms").autoplay then return end
  local s = nms.autoplay()
  if not s then return end
  local saved = ui.GetSetting("nms.ShowConsole", "0") == "1"
  if s.console == nil then -- (the module waits for the UI's saved state)
    nms.autoplay_console(saved)
    return
  end
  if s.console ~= saved then ui.SetSetting("nms.ShowConsole", s.console and "1" or "0") end
  if not s.console then
    wasShown = false
    return
  end
  T.frame()
  local appearing = not wasShown
  wasShown = true
  local rowH, gap = 72, 10
  local n = math.max(#s.rows, 1)
  local h = 24 * 2 + 92 + 48 + n * (rowH + gap) + 30 + 76
  if W.begin(NAME, { w = 1000, h = h, title = NAME, subtitle = "Your classes' automatic abilities",
        hints = { { "a", "Turn on / off" }, { "b", "Close" } } }) then
    local x0, y0, x1, y1 = W.area()
    local wx, wy = ui.GetWindowPos()
    local bw = T.px(380)
    local y = y0
    for i, r in ipairs(s.rows) do
      ui.SetCursorPos(x0 - wx, y - wy)
      if appearing and i == 1 then ui.SetKeyboardFocusHere() end
      if T.button("##auto" .. i, r.name, bw, T.px(rowH), { sub = r.enabled and "On: used for you" or "Off",
            primary = r.enabled, align = 0 }, pad) then
        nms.autoskill(r.name, not r.enabled)
      end
      local ready = r.left <= 0
      local bx0 = x0 + bw + T.px(20)
      local by0 = y + T.px(rowH) * 0.22
      T.font(20)
      T.bar(bx0, by0, x1, y + T.px(rowH) * 0.78, ready and 1 or r.fraction, ready and C.good or C.cast,
        ready and "Ready" or string.format("%.1fs", r.left))
      T.unfont()
      y = y + T.px(rowH + gap)
    end
    if #s.rows == 0 then
      T.font(21)
      T.text(x0, y + T.px(20), "No multiclass abilities available for this character.", C.faint)
      T.unfont()
      y = y + T.px(rowH + gap)
    end
    ui.DrawLine(x0, y + T.px(4), x1, y + T.px(4), C.ink[1], C.ink[2], C.ink[3], 0.08, 1)
    y = y + T.px(20)
    ui.SetCursorPos(x0 - wx, y - wy)
    if appearing and #s.rows == 0 then ui.SetKeyboardFocusHere() end
    local cd = ui.GetSetting("nms.ShowCooldowns", "0") == "1"
    if T.button("##cdwindow", "Show Cooldown Window", x1 - x0, T.px(66), { sub = cd and "On: the cooldowns on the HUD" or "Off",
          primary = cd, align = 0 }, pad) then
      ui.SetSetting("nms.ShowCooldowns", cd and "0" or "1")
    end
  end
  if W.finish() then nms.autoplay_console(false) end
end)
