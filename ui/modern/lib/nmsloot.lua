-- The NMS patch's Personal Loot window, its four tabs (NMS-Release Release-NMS-Client/eqgame_dll/loot_window.cpp,
-- the dinput8.dll the NMS-Release server ships; plugins/nms/nms_loot.h): Loot (the drops waiting for a decision),
-- Rules (the character's item rules, itemrules.ini), History (what the rules and the server did) and Settings (the
-- server's, saved on the character, and the window's own, nms.loot.* in this UI's settings). The state comes from
-- nms.loot(); the buttons send OP_PersonalLootAction (nms.loot_send) as the patch's do. Shared by the window
-- files of this pack (nmsloot.lua).
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local M = {}
local T = dofile("lib/theme.lua")
local C = T.color

-- PersonalLootActionType (common/eq_packet_structs.h)
local KEEP, SELL, LOOT_ALL, SELL_ALL, REFRESH, INSPECT = 1, 3, 5, 6, 7, 9
local SET_DEFER, SET_REPORT, SET_SELL_UNDER, GET_SETTINGS, INSPECT_ITEM, HISTORY = 14, 15, 16, 17, 18, 19
-- the rule types (nms::LootRule)
local NONE, ALWAYS_KEEP, ALWAYS_SELL, KEEP_ONCE, SELL_ONCE = 0, 1, 2, 3, 4
local RULE_LABEL = { [0] = "-", "Always Keep", "Always Sell", "Keep Once", "Sell Once" }

-- RuleTypeColor
local function ruleColor(t)
  if t == ALWAYS_KEEP then return T.rgba(C.pet) end
  if t == KEEP_ONCE then return T.rgba(C.good) end
  if t == ALWAYS_SELL then return T.rgba(C.gold) end
  if t == SELL_ONCE then return T.rgba(C.endurance) end
  return T.rgba(C.muted)
end

-- Money: "1p 2g 3s 4c", zero parts left out, "-" for nothing
function M.Money(copper)
  if copper == 0 then return "-" end
  local pp, gp, sp, cp = copper // 1000, (copper // 100) % 10, (copper // 10) % 10, copper % 10
  local s = ""
  if pp > 0 then s = s .. pp .. "p " end
  if gp > 0 then s = s .. gp .. "g " end
  if sp > 0 then s = s .. sp .. "s " end
  if cp > 0 or s == "" then s = s .. cp .. "c" end
  return (s:gsub(" +$", ""))
end

-- The window's own preferences (the patch's NMS_loot.ini [Loot]), handed to the module every frame.
local PREFS = {
  { "autoOpen", "AutoOpen", true, "Open automatically when loot needs a decision" },
  { "autoClose", "AutoClose", true, "Close automatically when the last item is decided" },
  { "pinned", "Pinned", false, "Pin (never auto-close)" },
  { "locked", "Locked", false, "Lock position and size" },
  { "countdown", "ShowCountdown", true, "Show countdown column" },
  { "sound", "Sound", true, "Beep when new loot needs a decision" },
}
function M.Prefs()
  local p = {}
  for _, e in ipairs(PREFS) do
    local v = ui.GetSetting("nms.loot." .. e[2])
    if v == nil then p[e[1]] = e[3] else p[e[1]] = v == "1" end
  end
  nms.loot_window("prefs", p)
  return p
end

local function icon(id, size)
  local tex, u0, v0, u1, v1 = eq.ItemIcon(id)
  if tex then ui.Image(tex, size, size, u0, v0, u1, v1) else ui.Dummy(size, size) end
end

local function rowHeight()
  local _, th = ui.CalcTextSize("Ag")
  return th + 6
end

-- RuleMenu: the right-click menu of a Loot row (with its pending id) or a Rules row.
local function ruleMenu(id, name, pending, cur)
  if ui.MenuItem("Inspect") then
    if pending then nms.loot_send(INSPECT, pending) else nms.loot_send(INSPECT_ITEM, 0, id) end
  end
  ui.Separator()
  ui.TextDisabled("Rule for " .. name)
  if ui.MenuItem("Always Keep", cur == ALWAYS_KEEP) then nms.loot_rule(id, name, ALWAYS_KEEP) end
  if ui.MenuItem("Always Sell", cur == ALWAYS_SELL) then nms.loot_rule(id, name, ALWAYS_SELL) end
  if ui.MenuItem("Keep Once (next drop)", cur == KEEP_ONCE) then nms.loot_rule(id, name, KEEP_ONCE) end
  if ui.MenuItem("Sell Once (next drop)", cur == SELL_ONCE) then nms.loot_rule(id, name, SELL_ONCE) end
  if cur ~= NONE then
    ui.Separator()
    if ui.MenuItem("Remove rule") then nms.loot_rule(id, name, NONE) end
  end
end

local function rulesById(st)
  local t = {}
  for _, r in ipairs(st.rules) do t[r.id] = r.type end
  return t
end

-- RenderLootTab
local function lootTab(st, prefs)
  local rh = rowHeight()
  local rows = st.rows
  if #rows == 0 then
    ui.Spacing()
    ui.TextDisabled("No loot waiting. Kill something.")
    ui.Spacing()
  end
  local tf = ui.TableFlags
  local flags = tf.RowBg | tf.BordersInnerH | tf.ScrollY | tf.SizingStretchProp
  local _, footer = ui.CalcTextSize("Ag")
  footer = footer + 18
  local tw = function(t) return (ui.CalcTextSize(t)) end
  if ui.BeginTable("##loot", 7, flags, 0, -footer) then
    local cf = ui.TableColumnFlags
    ui.TableSetupScrollFreeze(0, 1)
    ui.TableSetupColumn("##icon", cf.WidthFixed, rh + 4)
    ui.TableSetupColumn("Item", cf.WidthStretch, 4.0)
    ui.TableSetupColumn("Qty", cf.WidthFixed, tw("999") + 8)
    ui.TableSetupColumn("From", cf.WidthStretch, 2.5)
    ui.TableSetupColumn("Value", cf.WidthFixed, tw("999p 9g 9s 9c") + 8)
    ui.TableSetupColumn("Time", cf.WidthFixed, prefs.countdown and tw("00:00") + 8 or 1)
    ui.TableSetupColumn("##act", cf.WidthFixed, tw("Keep") + tw("Destroy") + tw("Always Keep") + tw("Always Sell") + 52)
    ui.TableHeadersRow()
    for _, r in ipairs(rows) do
      ui.PushID(r.pending)
      ui.TableNextRow()
      ui.TableNextColumn()
      icon(r.icon, rh)
      ui.TableNextColumn()
      ui.TextColored(C.text[1], C.text[2], C.text[3], 1.0, r.name)
      if ui.IsItemHovered() then
        ui.BeginTooltip()
        ui.Text(r.name)
        if r.lore then ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, "LORE") end
        if r.nodrop then ui.TextColored(C.bad[1], C.bad[2], C.bad[3], 1, "NO DROP") end
        if r.augmented then ui.TextColored(C.magic[1], C.magic[2], C.magic[3], 1, "Augmented") end
        if r.charges > 1 then ui.Text("Stack/charges: " .. r.charges) end
        ui.Separator()
        ui.TextDisabled("Right-click for rules / inspect")
        ui.EndTooltip()
      end
      if ui.BeginPopupContextItem("##rowmenu") then
        ruleMenu(r.item, r.name, r.pending, r.rule)
        ui.EndPopup()
      end
      if r.lore or r.nodrop then
        ui.SameLine()
        ui.TextDisabled((r.lore and "L" or "") .. (r.nodrop and "ND" or ""))
      end
      ui.TableNextColumn()
      if r.qty > 1 then ui.Text("x" .. r.qty) else ui.TextDisabled("1") end
      ui.TableNextColumn()
      ui.TextDisabled(r.npc)
      ui.TableNextColumn()
      if r.value > 0 then ui.Text(M.Money(r.value)) else ui.TextDisabled("none") end
      ui.TableNextColumn()
      if prefs.countdown then
        local t = string.format("%d:%02d", r.left // 60, r.left % 60)
        if r.left < 30 then ui.TextColored(C.hpLow[1], C.hpLow[2], C.hpLow[3], 1, t) else ui.TextDisabled(t) end
      end
      ui.TableNextColumn()
      if ui.SmallButton("Keep") then nms.loot_send(KEEP, r.pending) end
      if ui.IsItemHovered() then ui.SetTooltip("Loot this drop") end
      ui.SameLine()
      if ui.SmallButton(r.value > 0 and "Sell" or "Destroy") then nms.loot_send(SELL, r.pending) end
      if ui.IsItemHovered() then
        ui.SetTooltip(r.value > 0 and ("Sell this drop for " .. M.Money(r.value)) or "Destroy this drop (no vendor value)")
      end
      ui.SameLine()
      if ui.SmallButton("Always Keep") then
        nms.loot_rule(r.item, r.name, ALWAYS_KEEP)
        nms.loot_send(KEEP, r.pending)
      end
      if ui.IsItemHovered() then ui.SetTooltip("Loot it, and auto-loot this item from now on") end
      ui.SameLine()
      if ui.SmallButton("Always Sell") then
        nms.loot_rule(r.item, r.name, ALWAYS_SELL)
        nms.loot_send(SELL, r.pending)
      end
      if ui.IsItemHovered() then ui.SetTooltip("Sell it, and auto-sell this item from now on") end
      ui.PopID()
    end
    ui.EndTable()
  end
  -- the footer
  ui.BeginDisabled(#rows == 0)
  if ui.Button("Loot All") then nms.loot_send(LOOT_ALL) end
  ui.SameLine()
  if ui.Button("Sell All") then nms.loot_send(SELL_ALL) end
  if ui.IsItemHovered(ui.HoveredFlags.AllowWhenDisabled) then
    local total = 0
    for _, r in ipairs(rows) do total = total + r.value end
    ui.SetTooltip("Sell every pending item for " .. M.Money(total) .. " (worthless ones are destroyed)")
  end
  ui.EndDisabled()
  ui.SameLine()
  if ui.Button("Refresh") then nms.loot_send(REFRESH) end
  ui.SameLine()
  ui.TextDisabled(#rows .. " pending")
  if st.settings and st.settings.defer then
    ui.SameLine()
    ui.TextDisabled("| held in combat")
  end
end

-- RenderRulesTab
local search, addId, addType, sortCol, sortAsc = "", "", ALWAYS_KEEP, 2, true
local function typeCombo(id, cur, width)
  ui.SetNextItemWidth(width)
  ui.PushStyleColor(ui.Col.Text, ruleColor(cur))
  local open = ui.BeginCombo(id, RULE_LABEL[cur])
  ui.PopStyleColor(1)
  local picked
  if open then
    for t = NONE, SELL_ONCE do
      if ui.Selectable(RULE_LABEL[t], t == cur) then picked = t end
    end
    ui.EndCombo()
  end
  return picked
end

local function rulesTab(st)
  ui.SetNextItemWidth(180)
  search = ui.InputTextWithHint("##search", "search", search, 64)
  ui.SameLine()
  if ui.Button("Clear") then search = "" end
  ui.SameLine()
  if ui.Button("Reload file") then nms.loot_window("reload") end
  if ui.IsItemHovered() then ui.SetTooltip(st.rulesFile ~= "" and st.rulesFile or "(unknown character)") end
  ui.SameLine()
  local counts = { [0] = 0, 0, 0, 0, 0 }
  for _, r in ipairs(st.rules) do counts[r.type] = counts[r.type] + 1 end
  ui.TextDisabled(string.format("%d keep / %d sell / %d once", counts[ALWAYS_KEEP], counts[ALWAYS_SELL],
    counts[KEEP_ONCE] + counts[SELL_ONCE]))

  local tf = ui.TableFlags
  local flags = tf.RowBg | tf.BordersInnerH | tf.ScrollY | tf.SizingStretchProp | tf.Sortable
  local _, footer = ui.CalcTextSize("Ag")
  footer = footer + 18
  if ui.BeginTable("##rules", 4, flags, 0, -footer) then
    local cf = ui.TableColumnFlags
    ui.TableSetupScrollFreeze(0, 1)
    ui.TableSetupColumn("Rule", cf.WidthFixed, (ui.CalcTextSize("Always Keep")) + 40)
    ui.TableSetupColumn("Item ID", cf.WidthFixed, (ui.CalcTextSize("99999999")) + 8)
    ui.TableSetupColumn("Item", cf.WidthStretch | cf.DefaultSort, 4.0)
    ui.TableSetupColumn("##act", cf.WidthFixed | cf.NoSort, (ui.CalcTextSize("Remove")) + 12)
    ui.TableHeadersRow()
    local col, asc = ui.TableGetSortSpecs()
    if col then sortCol, sortAsc = col, asc end
    local order = {}
    for _, r in ipairs(st.rules) do order[#order + 1] = r end
    table.sort(order, function(a, b)
      local x, y
      if sortCol == 0 then x, y = a.type, b.type
      elseif sortCol == 1 then x, y = a.id, b.id
      else x, y = a.name:lower(), b.name:lower() end
      if x == y then return false end
      if sortAsc then return x < y else return x > y end
    end)
    local q = search:lower()
    local remove
    for _, f in ipairs(order) do
      if q == "" or f.name:lower():find(q, 1, true) or tostring(f.id):find(q, 1, true) then
        ui.PushID(f.id)
        ui.TableNextRow()
        ui.TableNextColumn()
        local t = typeCombo("##type", f.type, -1)
        if t then
          if t == NONE then remove = f.id else nms.loot_rule(f.id, "", t) end
        end
        ui.TableNextColumn()
        ui.TextDisabled(tostring(f.id))
        ui.TableNextColumn()
        ui.Text(f.name)
        if ui.BeginPopupContextItem("##rulemenu") then
          ruleMenu(f.id, f.name, nil, f.type)
          ui.EndPopup()
        end
        ui.TableNextColumn()
        if ui.SmallButton("Remove") then remove = f.id end
        ui.PopID()
      end
    end
    if remove then nms.loot_rule(remove, "", NONE) end
    ui.EndTable()
  end
  ui.SetNextItemWidth(110)
  addId = ui.InputText("##addid", addId, 10, ui.InputTextFlags.CharsDecimal)
  if ui.IsItemHovered() then ui.SetTooltip("Item ID") end
  ui.SameLine()
  addType = typeCombo("##addtype", addType, 130) or addType
  ui.SameLine()
  local id = tonumber(addId) or 0
  ui.BeginDisabled(id <= 0 or addType == NONE)
  if ui.Button("Add rule") then
    nms.loot_rule(id, "", addType)
    addId = ""
  end
  ui.EndDisabled()
  ui.SameLine()
  local file = st.rulesFile:match("([^/\\]+)$") or "?"
  ui.TextDisabled("or right-click any loot row. File: " .. file)
end

-- RenderHistoryTab
local function ago(age)
  if age < 60 then return age .. "s ago" elseif age < 3600 then return (age // 60) .. "m ago" end
  return (age // 3600) .. "h ago"
end

local function historyTab(st)
  local rh = rowHeight()
  if ui.Button("Refresh") then nms.loot_send(HISTORY) end
  ui.SameLine()
  if ui.Button("Clear") then nms.loot_window("clearHistory") end
  ui.SameLine()
  ui.TextDisabled("Rules and the auto-sell threshold, this session.")
  local tf = ui.TableFlags
  if ui.BeginTable("##hist", 6, tf.RowBg | tf.BordersInnerH | tf.ScrollY | tf.SizingStretchProp) then
    local cf = ui.TableColumnFlags
    ui.TableSetupScrollFreeze(0, 1)
    ui.TableSetupColumn("##icon", cf.WidthFixed, rh + 4)
    ui.TableSetupColumn("Action", cf.WidthFixed, (ui.CalcTextSize("Sold (Always Sell)")) + 8)
    ui.TableSetupColumn("Item", cf.WidthStretch, 4.0)
    ui.TableSetupColumn("From", cf.WidthStretch, 2.5)
    ui.TableSetupColumn("Coin", cf.WidthFixed, (ui.CalcTextSize("999p 9g 9s 9c")) + 8)
    ui.TableSetupColumn("When", cf.WidthFixed, (ui.CalcTextSize("999m ago")) + 8)
    ui.TableHeadersRow()
    -- the rules' own actions first, newest at the top
    for i, h in ipairs(st.localHistory) do
      ui.PushID(-i)
      ui.TableNextRow()
      ui.TableNextColumn()
      ui.Dummy(rh, rh)
      ui.TableNextColumn()
      local keep = h.rule == ALWAYS_KEEP or h.rule == KEEP_ONCE
      local r, g, b, a = ruleColor(h.rule)
      ui.TextColored(r, g, b, a, (keep and "Kept" or "Sold") .. " (" .. RULE_LABEL[h.rule] .. ")")
      ui.TableNextColumn()
      ui.Text(h.qty > 1 and (h.name .. " x" .. h.qty) or h.name)
      ui.TableNextColumn()
      ui.TextDisabled(h.npc)
      ui.TableNextColumn()
      ui.TextDisabled("-")
      ui.TableNextColumn()
      ui.TextDisabled(ago(h.age))
      ui.PopID()
    end
    -- then what the server resolved on its own (the sell-under threshold)
    for i, h in ipairs(st.history) do
      ui.PushID(i)
      ui.TableNextRow()
      ui.TableNextColumn()
      icon(h.icon, rh)
      ui.TableNextColumn()
      if h.action == 1 then ui.TextColored(C.pet[1], C.pet[2], C.pet[3], 1, "Kept (server)")
      elseif h.action == 2 then ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, "Sold (threshold)")
      else ui.TextColored(C.muted[1], C.muted[2], C.muted[3], 1, "Destroyed") end
      ui.TableNextColumn()
      ui.Text(h.qty > 1 and (h.name .. " x" .. h.qty) or h.name)
      if ui.IsItemClicked(ui.Mouse.Right) then nms.loot_send(INSPECT_ITEM, 0, h.item) end
      ui.TableNextColumn()
      ui.TextDisabled(h.npc)
      ui.TableNextColumn()
      if h.copper > 0 then ui.Text(M.Money(h.copper)) else ui.TextDisabled("-") end
      ui.TableNextColumn()
      ui.TextDisabled(ago(h.age))
      ui.PopID()
    end
    ui.EndTable()
  end
end

-- RenderSettingsTab
local sellUnder
local function settingsTab(st)
  ui.SeparatorText("Server (saved on your character)")
  local s = st.settings
  if not s then
    ui.TextDisabled("Waiting for the server...")
    if ui.Button("Request") then nms.loot_send(GET_SETTINGS) end
  else
    if not s.enabled then
      ui.TextColored(C.hpLow[1], C.hpLow[2], C.hpLow[3], 1, "Personal Loot is disabled on this server (Custom:Loot_Enabled).")
    end
    local defer, changed = ui.Checkbox("Hold loot until I'm out of combat", s.defer)
    if changed then nms.loot_send(SET_DEFER, 0, 0, defer and 1 or 0) end
    if ui.IsItemHovered() then
      ui.SetTooltip("Items still queue, but the window stays quiet and the auto-loot timer\nwaits until nothing is aggro on you.")
    end
    local report
    report, changed = ui.Checkbox("Chat summary of auto-sold items per kill", s.report)
    if changed then nms.loot_send(SET_REPORT, 0, 0, report and 1 or 0) end
    if sellUnder == nil or not sellUnder.editing then sellUnder = { text = tostring(s.sellUnder) } end
    ui.SetNextItemWidth(160)
    local text = ui.InputText("Auto-sell unknown items worth less than (copper)", sellUnder.text, 10,
      ui.InputTextFlags.CharsDecimal)
    sellUnder.editing = ui.IsItemActive()
    sellUnder.text = text
    local v = math.max(0, tonumber(text) or 0)
    ui.SameLine()
    if ui.Button("Apply") then nms.loot_send(SET_SELL_UNDER, 0, 0, v) end
    ui.TextDisabled("= " .. (v > 0 and M.Money(v) or "off")
      .. ". 0 turns it off. Happens on the server before the item reaches this window.")
    ui.TextDisabled("Undecided items are looted automatically after " .. s.timeout .. " seconds.")
  end
  ui.Spacing()
  ui.SeparatorText("Window (this computer)")
  for _, e in ipairs(PREFS) do
    local key = "nms.loot." .. e[2]
    local cur = ui.GetSetting(key)
    local on = (cur == nil and e[3]) or cur == "1"
    local nv, changed = ui.Checkbox(e[4], on)
    if changed then ui.SetSetting(key, nv and "1" or "0") end
  end
  ui.Spacing()
  ui.TextDisabled("Rules file: " .. (st.rulesFile ~= "" and st.rulesFile or "(unknown until in game)"))
  ui.TextDisabled("Commands: /loot [show|hide|toggle|pin|lock|rule <id> <keep|sell|keeponce|sellonce|none>|reset]")
  ui.TextDisabled("Server: #advloot, #lootpending, #loottriage, #lootdefer, #lootreport, #lootsellunder")
end

-- The window's title ("Personal Loot (n)" while rows wait) and its tabs; `tab` the tab /loot asked for.
function M.Title(st)
  if #st.rows == 0 then return "Personal Loot" end
  return "Personal Loot (" .. #st.rows .. ")"
end

function M.Body(st, prefs)
  if ui.BeginTabBar("##tabs") then
    local function item(label, i, fn)
      if ui.BeginTabItem(label, st.tab == i and ui.TabItemFlags.SetSelected or 0) then
        fn()
        ui.EndTabItem()
      end
    end
    item("Loot", 0, function() lootTab(st, prefs) end)
    item("Rules", 1, function() rulesTab(st) end)
    item("History", 2, function() historyTab(st) end)
    item("Settings", 3, function() settingsTab(st) end)
    ui.EndTabBar()
  end
end

return M
