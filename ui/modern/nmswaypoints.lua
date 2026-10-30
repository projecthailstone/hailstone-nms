-- Attune the Map: this server's waypoint window (the patch's WaypointsWnd: nms.waypoints(), plugins/nms/nms_ui.cpp)
-- in this UI's dialog frame. It opens with each list the server sends (when the player clicks the Magic Map or a
-- waypoint) and with /waypoint. The continents at the left (the patch's names, one per category the list uses, the
-- first selected), that continent's waypoints at the right with a search box (a locked one greyed, as the window
-- colours it), the Port Group and Auto-Confirm boxes (a click tells the server, as the window's), and the travel
-- buttons; a double click on a waypoint is its Travel. Both travel buttons close it, as the patch's do.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local hud = dofile("lib/hud.lua")
local T = hud.T
local C = T.color

local open, serial = false, nil
local continent, selected = 1, nil -- the continent's row; the waypoint's id
local search = ""

-- The categories the list uses, ascending (WaypointsWnd's std::set).
local function categories(wl)
  local seen, out = {}, {}
  for _, e in ipairs(wl.entries) do
    if not seen[e.category] then
      seen[e.category] = true
      out[#out + 1] = e.category
    end
  end
  table.sort(out)
  return out
end

-- A list as a table of one column: rows of { text, key, greyed }; returns the clicked row's key and whether it was
-- a double click.
local function list(id, w, h, rows, current)
  local clicked, double = nil, false
  local flags = ui.TableFlags.RowBg | ui.TableFlags.ScrollY
  if not ui.BeginTable(id, 1, flags, w, h) then return nil, false end
  for _, r in ipairs(rows) do
    ui.TableNextRow()
    ui.TableNextColumn()
    if r[3] then ui.PushStyleColor(ui.Col.Text, 0.63, 0.63, 0.63, 1) end -- the window's 0xFFA0A0A0
    if ui.Selectable(r[1] .. "##" .. id .. r[2], r[2] == current, ui.SelectableFlags.AllowDoubleClick) then
      clicked = r[2]
      double = ui.IsMouseDoubleClicked(ui.Mouse.Left)
    end
    if r[3] then ui.PopStyleColor() end
  end
  ui.EndTable()
  return clicked, double
end

local function button(label, w, h, enabled, primary, tooltip)
  if primary then
    ui.PushStyleColor(ui.Col.Button, C.accent[1], C.accent[2], C.accent[3], 0.24)
    ui.PushStyleColor(ui.Col.ButtonHovered, C.accent[1], C.accent[2], C.accent[3], 0.40)
    ui.PushStyleColor(ui.Col.ButtonActive, C.accent[1], C.accent[2], C.accent[3], 0.55)
  end
  ui.BeginDisabled(not enabled)
  local clicked = ui.Button(label, w, h)
  ui.EndDisabled()
  if primary then ui.PopStyleColor(3) end
  if tooltip and ui.IsItemHovered(ui.HoveredFlags.AllowWhenDisabled) then ui.SetTooltip(tooltip) end
  return clicked and enabled
end

ui.window("Attune the Map", function()
  if not eq.InGame() or not nms then
    open = false
    return
  end
  local wl = nms.waypoints()
  if not wl then
    open = false
    return
  end
  if wl.serial ~= serial then -- HandleWaypointList, then Show
    serial, open, continent, selected = wl.serial, true, 1, nil
  end
  if not open then return end
  local cats = categories(wl)
  if continent > #cats then continent = 1 end
  local function travel(id, expedition)
    nms.waypoint_request(id, expedition, wl.group, wl.autoconfirm)
    open = false
  end
  hud.Dialog("WaypointsWnd", "Attune the Map", {
    shown = true,
    size = function() return 540, 380 end,
    resizable = { 380, 260 },
    onClose = function() open = false end,
  }, function(_, _, w, _, s)
    ui.TextColored(C.muted[1], C.muted[2], C.muted[3], 1, "Which destination awaits you?")
    local _, lh = ui.CalcTextSize("Ag")
    local btnH = lh + 10 * s
    local below = btnH + 8 * s + lh + 12 * s
    -- the continents
    local leftW = math.max(120 * s, math.min(180 * s, w * 0.3))
    local rows = {}
    for i, c in ipairs(cats) do rows[i] = { wl.continents[c + 1] or "Unknown", i } end
    local picked = list("##continents", leftW, -below, rows, continent)
    if picked and picked ~= continent then continent, selected = picked, nil end
    ui.SameLine()
    -- the waypoints, filtered by the search box
    ui.BeginGroup()
    ui.SetNextItemWidth(-1)
    search = ui.InputTextWithHint("##wpsearch", "Search waypoints", search, 64)
    local needle = search:lower()
    rows = {}
    for _, e in ipairs(wl.entries) do
      if e.category == cats[continent] and (needle == "" or e.name:lower():find(needle, 1, true)) then
        rows[#rows + 1] = { e.name, e.id, not e.enabled }
      end
    end
    local id, double = list("##waypoints", 0, -below, rows, selected)
    if id then selected = id end
    if id and double then travel(id, false) end
    ui.EndGroup()
    -- the boxes and the buttons
    ui.BeginDisabled(not wl.groupEnabled)
    local group, gchanged = ui.Checkbox("Port Group", wl.group)
    ui.EndDisabled()
    if ui.IsItemHovered(ui.HoveredFlags.AllowWhenDisabled) then ui.SetTooltip("Take your group with you") end
    if gchanged then nms.waypoint_request(0, false, group, wl.autoconfirm) end
    ui.SameLine(0, 24 * s)
    local auto, achanged = ui.Checkbox("Auto-Confirm", wl.autoconfirm)
    if ui.IsItemHovered() then ui.SetTooltip("Travel without asking to confirm") end
    if achanged then nms.waypoint_request(0, false, wl.group, auto) end
    local bw = (ui.GetContentRegionAvail() - 8) / 2
    if button("Travel to Waypoint", bw, btnH, selected ~= nil, true,
        selected and "Travel to the selected waypoint" or "Pick a waypoint first") then
      travel(selected, false)
    end
    ui.SameLine()
    if button("Travel to Expedition", bw, btnH, wl.expeditionEnabled, false, "Travel to your expedition") then
      travel(0, true)
    end
  end)
end)
