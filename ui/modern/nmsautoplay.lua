-- Multiclass Autoplay Console (this server's console, nms.autoplay: plugins/nms/nms_ui.cpp) in this UI's dialog
-- frame: for each auto-skill the character's classes have, a check box (the server's autoskill bit; a click asks the
-- server to turn it on or off) and its cooldown bar, green "Ready" when it is back; then "Show Cooldown Window"
-- (nms.ShowCooldowns). Shown per nms.ShowConsole and toggled with /autoskill; the close box hides it.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local hud = dofile("lib/hud.lua")
local T = hud.T
local C = T.color
local READY = C.pet -- (the palette's green)

ui.window("Multiclass Autoplay Console", function()
  local st = nms.autoplay()
  if not st then return end
  local saved = ui.GetSetting("nms.ShowConsole", "0") == "1"
  if st.console == nil then
    nms.autoplay_console(saved)
    return
  end
  if st.console ~= saved then ui.SetSetting("nms.ShowConsole", st.console and "1" or "0") end
  hud.Dialog("NmsAutoplayWnd", "Autoplay Console", {
    shown = st.console,
    keepTitle = true,
    size = function() return 360, 60 + math.max(#st.rows, 1) * 30 end,
    onClose = function() nms.autoplay_console(false) end,
  }, function(x, y, w, h, s)
    for _, r in ipairs(st.rows) do
      local on, changed = ui.Checkbox(r.name, r.enabled)
      if changed then nms.autoskill(r.name, on) end
      ui.SameLine(160 * s, -1)
      local ready = r.left <= 0
      local c = ready and READY or C.cast
      ui.PushStyleColor(ui.Col.PlotHistogram, c[1], c[2], c[3], 1.0)
      ui.ProgressBar(ready and 1 or r.fraction, -1, 0, ready and "Ready" or string.format("%.1fs", r.left))
      ui.PopStyleColor(1)
    end
    if #st.rows == 0 then ui.TextDisabled("No multiclass abilities available for this character.") end
    ui.Separator()
    local cd = ui.GetSetting("nms.ShowCooldowns", "0") == "1"
    local v, changed = ui.Checkbox("Show Cooldown Window", cd)
    if changed then ui.SetSetting("nms.ShowCooldowns", v and "1" or "0") end
  end)
end)
