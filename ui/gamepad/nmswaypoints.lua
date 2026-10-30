-- Attune the Map (gamepad pack): this server's waypoint window (the patch's WaypointsWnd: nms.waypoints(),
-- plugins/nms/nms_ui.cpp), which opens with each list the server sends and with /waypoint. The continents on the left
-- (LB / RB go through them too), that continent's waypoints in the middle (a locked one greyed, as the window colours
-- it; A selects one, X travels to it), and on the right Port Group and Auto-Confirm (a click tells the server, as the
-- window's check boxes) and Travel to Waypoint / Travel to Expedition, which close it as the patch's do.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local C = T.C
local NAME = "Attune the Map"
local LOCKED = { 0.63, 0.63, 0.63, 1 } -- the window's 0xFFA0A0A0

local open, serial = false, nil
local continent, selected = 1, nil -- the continent's row; the waypoint's id
local continents, waypoints = {}, {}
local side = {}

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

-- rows of { text, key, greyed } as a pad list; returns the focused row's key.
local function list(id, state, rows, current, width, height, onA)
  local rowH = 56
  local x0, y0 = ui.GetCursorScreenPos()
  if #rows == 0 then
    T.font(20)
    T.text(x0 + T.px(6), y0 + T.px(10), "Nothing here.", C.faint)
    T.unfont()
    ui.Dummy(width, T.px(rowH))
    return nil
  end
  local visible = math.max(1, math.floor((height + T.px(10)) / (T.px(rowH) + T.px(10))))
  state.selected = nil
  for i, r in ipairs(rows) do if r[2] == current then state.selected = i end end
  local f = W.list(id, state, #rows, visible, rowH, width, function(i, rx0, ry0, rx1, ry1)
    local r = rows[i]
    local col = (i == state.selected and C.accent) or (r[3] and LOCKED) or C.text
    T.font(21)
    T.textIn(rx0 + T.px(18), ry0, rx1 - T.px(14), ry1, T.fit(r[1], rx1 - rx0 - T.px(40)), col, 0)
    T.unfont()
  end, function(i) onA(rows[i][2]) end)
  return f and rows[f][2]
end

ui.window(NAME, function()
  T.frame()
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
    waypoints.focusNext = 1
  end
  if not open then return end
  local cats = categories(wl)
  if continent > #cats then continent = 1 end
  local function travel(id, expedition)
    nms.waypoint_request(id, expedition, wl.group, wl.autoconfirm)
    open = false
  end
  if W.begin(NAME, { w = 1600, h = 940, title = NAME, subtitle = "Which destination awaits you?",
        hints = { { "lb", "Continent" }, { "rb", "Continent" }, { "a", "Select" }, { "x", "Travel" }, { "b", "Close" } } }) then
    local x0, y0, x1, y1, cw = W.area()
    local wx, wy = ui.GetWindowPos()
    -- LB / RB: the previous / next continent
    if #cats > 0 then
      local step = (W.pressed("rb") and 1 or 0) - (W.pressed("lb") and 1 or 0)
      if step ~= 0 then
        continent, selected = (continent - 1 + step) % #cats + 1, nil
        waypoints.offset, waypoints.focusNext = 0, 1
      end
    end
    local cW = cw * 0.24
    local wW = cw * 0.46
    local rows = {}
    for i, c in ipairs(cats) do rows[i] = { wl.continents[c + 1] or "Unknown", i } end
    ui.SetCursorPos(x0 - wx, y0 - wy)
    list("continents", continents, rows, continent, cW, y1 - y0, function(i)
      if i ~= continent then continent, selected = i, nil end
    end)
    rows = {}
    for _, e in ipairs(wl.entries) do
      if e.category == cats[continent] then rows[#rows + 1] = { e.name, e.id, not e.enabled } end
    end
    ui.SetCursorPos(x0 + cW + T.px(20) - wx, y0 - wy)
    local row = list("waypoints", waypoints, rows, selected, wW, y1 - y0, function(id) selected = id end)
    if row and W.pressed("x") then travel(row, false) end
    W.toSide(side, row)
    local rx0 = x0 + cW + wW + T.px(40)
    local rw = x1 - rx0
    local bh = T.px(80)
    local buttons = {
      function()
        if T.button("##wpgroup", "Port Group", rw, bh, { sub = wl.group and "Your group comes too" or "Only you",
              primary = wl.group, disabled = not wl.groupEnabled }, pad) and wl.groupEnabled then
          nms.waypoint_request(0, false, not wl.group, wl.autoconfirm)
        end
      end,
      function()
        if T.button("##wpauto", "Auto-Confirm", rw, bh, { sub = wl.autoconfirm and "No confirmation" or "Asks first",
              primary = wl.autoconfirm }, pad) then
          nms.waypoint_request(0, false, wl.group, not wl.autoconfirm)
        end
      end,
      function()
        if T.button("##wptravel", "Travel to Waypoint", rw, bh, { primary = true, glyph = "x", disabled = selected == nil,
              sub = selected and "The selected one" or "Pick a waypoint first" }, pad) and selected then
          travel(selected, false)
        end
      end,
      function()
        if T.button("##wpexp", "Travel to Expedition", rw, bh, { sub = "Your expedition",
              disabled = not wl.expeditionEnabled }, pad) and wl.expeditionEnabled then
          travel(0, true)
        end
      end,
    }
    local by = y0
    for k, f in ipairs(buttons) do
      ui.SetCursorPos(rx0 - wx, by - wy)
      W.sideBefore(side, k)
      f()
      W.sideAfter(side, k, #buttons, waypoints)
      by = by + bh + T.px(12)
    end
  end
  if W.finish() then open = false end
end)
