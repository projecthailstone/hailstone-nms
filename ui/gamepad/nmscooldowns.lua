-- Autoplay Cooldowns (gamepad pack): this server's own cooldown window (nms.cooldowns: plugins/nms/nms_ui.cpp,
-- shown per nms.ShowCooldowns as in the other packs), a piece of the HUD that never takes the pad's focus: each
-- active auto-skill's name and a bar that fills as it comes back, "Ready" when it is; its close button clears the
-- setting.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local T = dofile("lib/theme.lua")
local W = dofile("lib/window.lua")
local C = T.C

-- A HUD window (as hudextras.lua's) at x, y (design px; negative: from the right / bottom), w x h; inputs: the mouse
-- may click in it.
local function hud(name, x, y, w, h, inputs, draw)
  T.frame()
  T.resetFonts()
  local px, py, pw, ph = T.px(x), T.px(y), T.px(w), T.px(h)
  if px < 0 then px = T.W + px - pw end
  if py < 0 then py = T.H + py - ph end
  ui.SetNextWindowPos(px, py, ui.Cond.Always)
  ui.SetNextWindowSize(pw, ph, ui.Cond.Always)
  ui.PushStyleVar(ui.StyleVar.WindowPadding, 0, 0)
  ui.PushStyleVar(ui.StyleVar.WindowBorderSize, 0)
  local flags = ui.WindowFlags.NoDecoration | ui.WindowFlags.NoBackground | ui.WindowFlags.NoSavedSettings
      | ui.WindowFlags.NoMove | ui.WindowFlags.NoFocusOnAppearing | ui.WindowFlags.NoBringToFrontOnFocus | W.NoNav
  if not inputs then flags = flags | ui.WindowFlags.NoInputs end
  if ui.Begin(name, nil, flags) then
    T.font(T.BASE)
    draw(px, py, px + pw, py + ph)
    T.unfont()
  end
  ui.End()
  ui.PopStyleVar(2)
end

local READY = C.good
ui.window("HUD Autoplay Cooldowns", function()
  if not eq.InGame() or ui.GetSetting("nms.ShowCooldowns", "0") ~= "1" or not nms then return end
  local rows = nms.cooldowns()
  if not rows then return end
  local rowH = 54
  hud("HUD Autoplay Cooldowns", -24, 760 - math.max(#rows, 1) * rowH, 320, 52 + math.max(#rows, 1) * rowH, true, function(x0, y0, x1, y1)
    T.panel(x0, y0, x1, y1, 0.88, T.px(14))
    local p = T.px(14)
    T.font(17)
    T.text(x0 + p, y0 + T.px(10), "AUTOPLAY COOLDOWNS", C.dim)
    T.unfont()
    -- the close box (clears nms.ShowCooldowns, as the patch's window's does)
    local cs = T.px(28)
    local wx, wy = ui.GetWindowPos()
    ui.SetCursorPos(x1 - p - cs - wx, y0 + T.px(8) - wy)
    if ui.InvisibleButton("##cdclose", cs, cs) then ui.SetSetting("nms.ShowCooldowns", "0") end
    local hov = ui.IsItemHovered()
    T.font(18)
    T.textIn(x1 - p - cs, y0 + T.px(8), x1 - p, y0 + T.px(8) + cs, "x", hov and C.accent or C.dim, 0.5)
    T.unfont()
    local y = y0 + T.px(44)
    if #rows == 0 then
      T.font(18)
      T.text(x0 + p, y + T.px(6), "No active auto-skills.", C.faint)
      T.unfont()
      return
    end
    for _, r in ipairs(rows) do
      local ready = r[2] <= 0
      T.font(18)
      local _, th = ui.CalcTextSize("Ag")
      T.textIn(x0 + p, y, x1 - p - T.px(80), y + th, T.fit(r[1], x1 - x0 - 2 * p - T.px(80)), C.text, 0)
      T.textIn(x0 + p, y, x1 - p, y + th, ready and "Ready" or string.format("%.1fs", r[2]), ready and READY or C.dim, 1)
      T.unfont()
      T.bar(x0 + p, y + th + T.px(4), x1 - p, y + th + T.px(14), ready and 1 or (1 - (r[3] or 0)), ready and READY or C.cast)
      y = y + T.px(rowH)
    end
  end)
end)
