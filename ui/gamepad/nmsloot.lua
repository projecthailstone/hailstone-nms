-- Personal Loot (gamepad pack): this server's loot window (the NMS patch's, plugins/nms/nms_loot.h; nms.loot), its
-- four tabs on LB / RB as the Modern UI's (plugins/nms/ui/modern/lib/nmsloot.lua) and the patch's:
--   Loot      the drops waiting for a decision, one row each (icon, name, LORE / NO DROP, from, value, the time left
--             before it is looted for you). The window takes the pad when it pops up, on the first row: A keeps
--             the focused drop, X sells it (destroys a worthless one), Y opens its menu (Always Keep / Always Sell
--             and act now, a rule alone, inspect). A decided row's place goes to the next, so A A A loots a corpse
--             through. Beside the rows: the focused drop's facts, the same actions as buttons (the mouse selects a
--             row with a click), Loot All (RT), Sell All (asked first) and Refresh
--   Rules     the item rules (itemrules.ini): a search typed on the on-screen keys, the order, Reload file; A changes
--             a rule's type, X removes it, Y inspects the item; an item ID and a type to add one
--   History   what the rules and the server did this session; Refresh, Clear; Y (or A) inspects a server row
--   Settings  the server's (hold loot in combat, the chat summary, the auto-sell threshold typed on the keys, saved
--             on the character) and the window's own (nms.loot.* in this pack's settings, as the other packs)
-- The buttons send OP_PersonalLootAction (nms.loot_send) and set rules (nms.loot_rule) as the patch's do. It opens
-- when loot needs a decision and with /loot; B hides it. On a stock server nms.loot() is nil.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local CH = dofile("lib/child.lua")
local M = dofile("lib/markets.lua")
local C = T.C
local NAME = "Personal Loot"

-- PersonalLootActionType (common/eq_packet_structs.h)
local KEEP, SELL, LOOT_ALL, SELL_ALL, REFRESH, INSPECT = 1, 3, 5, 6, 7, 9
local SET_DEFER, SET_REPORT, SET_SELL_UNDER, GET_SETTINGS, INSPECT_ITEM, HISTORY = 14, 15, 16, 17, 18, 19
-- the rule types (nms::LootRule)
local NONE, ALWAYS_KEEP, ALWAYS_SELL, KEEP_ONCE, SELL_ONCE = 0, 1, 2, 3, 4
local RULE_LABEL = { [0] = "No rule", "Always Keep", "Always Sell", "Keep Once", "Sell Once" }

local function ruleColor(t)
  if t == ALWAYS_KEEP then return C.good end
  if t == KEEP_ONCE then return C.mana end
  if t == ALWAYS_SELL then return C.gold end
  if t == SELL_ONCE then return C.endurance end
  return C.faint
end

-- "1p 2g 3s 4c", zero parts left out, "-" for nothing (the patch's coin text)
local function money(copper)
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
  { "autoOpen", "AutoOpen", true, "Open by itself", "When loot needs a decision" },
  { "autoClose", "AutoClose", true, "Close by itself", "When the last drop is decided" },
  { "pinned", "Pinned", false, "Pinned", "Never closes by itself" },
  { "locked", "Locked", false, "Locked", "Position and size (the other UIs' window)" },
  { "countdown", "ShowCountdown", true, "Show the countdown", "The time left on each drop" },
  { "sound", "Sound", true, "Sound", "Beep when new loot needs a decision" },
}
local function prefs()
  local p = {}
  for _, e in ipairs(PREFS) do
    local v = ui.GetSetting("nms.loot." .. e[2])
    if v == nil then p[e[1]] = e[3] else p[e[1]] = v == "1" end
  end
  nms.loot_window("prefs", p)
  return p
end

local st = CH.new()            -- the keyboard and the pickers
local ask = {}                 -- W.confirm's question (Sell All)
local page = 1
local loot, rules, hist = {}, {}, {}
local side = {}
local wasShown = false
local selPending                -- the selected drop (its pending id): the side buttons act on it
local focusBtn                  -- a button of the Loot tab to take the pad's focus next frame
local rowModal                  -- the list whose row opened the picker up now (its row takes the focus back)
local afterModal                -- else the button that did
local wasBusy = false
local search, addId, addType, sortBy = "", "", ALWAYS_KEEP, 1
local SORTS = { "Item name", "Item ID", "Rule" }
local sellUnder                 -- the threshold being typed (nil: the server's)

-- The pad's focus among a list's rows (W.list state): f the focused row this frame. When the focused row leaves the
-- list (decided, removed) the focus goes to the row now in its place (or the last), so A A A goes on down the list;
-- with none left, to `empty` (a button id) when given.
local function holdFocus(state, f, count, visible, empty)
  local off = state.offset or 0
  if f then
    state.v = f - off
  elseif state.had and state.v and count < (state.count or 0) and W.focused() then
    if count > 0 then state.focusNext = math.max(1, math.min(state.v, visible, count - off)) else focusBtn = empty end
  end
  state.had, state.count = f ~= nil, count
end

-- Before the list: a row asked to take the focus that is no longer there takes the last one's place (or `empty`'s).
local function clampFocus(state, count, visible, empty)
  if not state.focusNext then return end
  local n = math.min(visible, count - (state.offset or 0))
  if n >= 1 then state.focusNext = math.min(state.focusNext, n) else state.focusNext, focusBtn = nil, empty end
end

local function keep(r) nms.loot_send(KEEP, r.pending) end
local function sell(r) nms.loot_send(SELL, r.pending) end

-- A toggle button: the label, a line under it (on / off), lit while on.
local function toggle(id, label, on, sub, w, h)
  return T.button(id, label, w, h, { sub = (on and "On" or "Off") .. (sub and ("  -  " .. sub) or ""), primary = on,
    align = 0 }, pad)
end

-- A drop's menu (Y): rules and inspect, the patch's RuleMenu with the Loot tab's Always Keep / Always Sell buttons.
local function dropMenu(r)
  local choices, acts = {}, {}
  local function add(t, f) choices[#choices + 1] = t; acts[#acts + 1] = f end
  add("Always Keep: loot it now and from now on", function() nms.loot_rule(r.item, r.name, ALWAYS_KEEP) keep(r) end)
  add("Always Sell: sell it now and from now on", function() nms.loot_rule(r.item, r.name, ALWAYS_SELL) sell(r) end)
  add("Rule only: Always Keep", function() nms.loot_rule(r.item, r.name, ALWAYS_KEEP) end)
  add("Rule only: Always Sell", function() nms.loot_rule(r.item, r.name, ALWAYS_SELL) end)
  add("Rule only: Keep Once (the next drop)", function() nms.loot_rule(r.item, r.name, KEEP_ONCE) end)
  add("Rule only: Sell Once (the next drop)", function() nms.loot_rule(r.item, r.name, SELL_ONCE) end)
  if r.rule ~= NONE then add("Remove the rule (" .. RULE_LABEL[r.rule] .. ")", function() nms.loot_rule(r.item, r.name, NONE) end) end
  add("Inspect", function() nms.loot_send(INSPECT, r.pending) end)
  CH.choose(st, r.name, choices, nil, function(i) acts[i]() end)
end

-- A rule's type picker (A on a Rules row, and the type to add).
local function typeMenu(caption, cur, onPick, from)
  local choices = {}
  for t = ALWAYS_KEEP, SELL_ONCE do choices[#choices + 1] = RULE_LABEL[t] end
  choices[#choices + 1] = "Remove the rule"
  local colors = { ruleColor(ALWAYS_KEEP), ruleColor(ALWAYS_SELL), ruleColor(KEEP_ONCE), ruleColor(SELL_ONCE), C.bad }
  CH.choose(st, caption, choices, cur ~= NONE and cur or nil, function(i) onPick(i <= 4 and i or NONE) end, from)
  st.pick.colors = colors
end

-- Coloured words one after the other from x at y; returns the end x.
local function words(x, y, list)
  for _, p in ipairs(list) do
    x = x + T.text(x, y, p[1], p[2]) + T.px(12)
  end
  return x
end

-- ---------------------------------------------------------------- Loot
local function lootRow(r, x0, y0, x1, y1, pr)
  local s = y1 - y0 - T.px(16)
  T.itemIcon(r.icon, x0 + T.px(10), y0 + T.px(8), x0 + T.px(10) + s, y0 + T.px(8) + s)
  local x = x0 + s + T.px(24)
  local tw = T.px(pr.countdown and 90 or 0)
  local vw = T.px(210)
  local nx1 = x1 - tw - vw - T.px(20)
  local mid = (y0 + y1) / 2
  T.font(22)
  local _, th = ui.CalcTextSize("Ag")
  T.textIn(x, mid - th, nx1, mid, T.fit(r.qty > 1 and (r.name .. "  x" .. r.qty) or r.name, nx1 - x), C.text, 0)
  T.unfont()
  T.font(17)
  local flags = {}
  if r.lore then flags[#flags + 1] = { "LORE", C.gold } end
  if r.nodrop then flags[#flags + 1] = { "NO DROP", C.bad } end
  if r.augmented then flags[#flags + 1] = { "AUGMENTED", C.mana } end
  if r.rule ~= NONE then flags[#flags + 1] = { RULE_LABEL[r.rule]:upper(), ruleColor(r.rule) } end
  ui.PushClipRect(x, mid, nx1, y1, true)
  local fx = words(x, mid + T.px(2), flags)
  T.text(fx, mid + T.px(2), "from " .. r.npc, C.dim)
  ui.PopClipRect()
  T.unfont()
  T.font(20)
  local vx1 = x1 - tw - T.px(16)
  if r.value > 0 then M.price(r.value, vx1, y0, y1, 1) else T.textIn(vx1 - vw, y0, vx1, y1, "no value", C.faint, 1) end
  if pr.countdown then
    local t = string.format("%d:%02d", r.left // 60, r.left % 60)
    T.textIn(x1 - tw, y0, x1 - T.px(16), y1, t, r.left < 30 and C.bad or C.dim, 1)
  end
  T.unfont()
end

local function lootTab(s, pr, x0, y0, x1, y1, appearing)
  local wx, wy = ui.GetWindowPos()
  local rows = s.rows
  local rx0 = x1 - T.px(460)
  local lw = rx0 - T.px(24) - x0
  local rowH = 80
  local visible = math.max(1, math.floor((y1 - y0 + T.px(10)) / (T.px(rowH) + T.px(10))))
  -- the selection: the drop last focused (or clicked), else the first
  local sel, selI
  for i, r in ipairs(rows) do if r.pending == selPending then sel, selI = r, i end end
  if not sel and rows[1] then sel, selI, selPending = rows[1], 1, rows[1].pending end
  loot.selected = selI
  if appearing and #rows > 0 then loot.offset, loot.focusNext = 0, 1 end
  clampFocus(loot, #rows, visible, "##refresh")
  ui.SetCursorPos(x0 - wx, y0 - wy)
  local f
  if #rows == 0 then
    T.font(24)
    T.text(x0 + T.px(6), y0 + T.px(16), "No loot waiting. Kill something.", C.dim)
    T.unfont()
    T.font(19)
    T.text(x0 + T.px(6), y0 + T.px(56), "Drops your rules do not decide wait here.", C.faint)
    T.unfont()
    ui.Dummy(lw, T.px(rowH))
  else
    f = W.list("nmsloot", loot, #rows, visible, rowH, lw, function(i, rx0_, ry0, rx1, ry1)
      lootRow(rows[i], rx0_, ry0, rx1, ry1, pr)
    end, function(i)
      selPending = rows[i].pending
      if pad.navVisible() then keep(rows[i]) end -- (A keeps; a mouse click only selects)
    end)
  end
  if f then
    local r = rows[f]
    selPending, sel = r.pending, r
    if W.pressed("x") then sell(r) end
    if W.pressed("y") then
      dropMenu(r)
      rowModal = loot
    end
  end
  holdFocus(loot, f, #rows, visible, "##refresh")
  W.toSide(side, f)
  -- the selected drop's facts
  local y = y0
  CH.caption(rx0, y, sel and "Selected" or "Nothing selected")
  y = y + T.px(26)
  if sel then
    T.font(24)
    T.text(rx0, y, T.fit(sel.name, x1 - rx0), C.text)
    T.unfont()
    y = y + T.px(36)
    T.font(19)
    local lines = {
      { "From", sel.npc, C.text },
      { "Quantity", tostring(sel.qty) .. (sel.charges > 1 and ("  (charges " .. sel.charges .. ")") or ""), C.text },
      { "Value", sel.value > 0 and money(sel.value) or "none (Sell destroys it)", sel.value > 0 and C.gold or C.faint },
      { "Rule", RULE_LABEL[sel.rule], ruleColor(sel.rule) },
      { "Looted in", T.duration(sel.left) .. " if undecided", sel.left < 30 and C.bad or C.dim },
    }
    for _, l in ipairs(lines) do
      T.text(rx0, y, l[1], C.dim)
      T.text(rx0 + T.px(130), y, T.fit(l[2], x1 - rx0 - T.px(130)), l[3])
      y = y + T.px(28)
    end
    local flags = {}
    if sel.lore then flags[#flags + 1] = { "LORE", C.gold } end
    if sel.nodrop then flags[#flags + 1] = { "NO DROP", C.bad } end
    if sel.augmented then flags[#flags + 1] = { "AUGMENTED", C.mana } end
    words(rx0, y, flags)
    T.unfont()
  end
  -- the actions (a grid of two)
  local gap, bh = T.px(12), T.px(64)
  local bw = (x1 - rx0 - gap) / 2
  local by = y1 - 4 * bh - 3 * gap
  local function at(c, r) ui.SetCursorPos(rx0 + c * (bw + gap) - wx, by + r * (bh + gap) - wy) end
  local function btn(id, label, opts, fn)
    if focusBtn == id then
      ui.SetKeyboardFocusHere()
      pad.showCursor()
      focusBtn = nil
    end
    if T.button(id, label, bw, bh, opts, pad) then fn() end
  end
  local none = sel == nil
  at(0, 0)
  W.sideBefore(side, 1)
  btn("##keep", "Keep", { primary = true, glyph = "a", disabled = none }, function() keep(sel) end)
  if ui.IsItemFocused and ui.IsItemFocused() then side.want = nil end
  at(1, 0)
  btn("##sell", sel and sel.value == 0 and "Destroy" or "Sell", { glyph = "x", disabled = none }, function() sell(sel) end)
  at(0, 1)
  btn("##akeep", "Always Keep", { disabled = none }, function() nms.loot_rule(sel.item, sel.name, ALWAYS_KEEP) keep(sel) end)
  at(1, 1)
  btn("##asell", "Always Sell", { disabled = none }, function() nms.loot_rule(sel.item, sel.name, ALWAYS_SELL) sell(sel) end)
  at(0, 2)
  btn("##menu", "Rules, Inspect", { glyph = "y", disabled = none }, function() dropMenu(sel) afterModal = "##menu" end)
  at(1, 2)
  btn("##refresh", "Refresh", {}, function() nms.loot_send(REFRESH) end)
  at(0, 3)
  btn("##lootall", "Loot All", { glyph = "rt", disabled = #rows == 0 }, function() nms.loot_send(LOOT_ALL) end)
  at(1, 3)
  btn("##sellall", "Sell All", { disabled = #rows == 0 }, function()
    local total = 0
    for _, r in ipairs(rows) do total = total + r.value end
    ask.confirm = { "Sell every waiting drop for " .. money(total) .. "? (Worthless ones are destroyed.)",
      function() nms.loot_send(SELL_ALL) end }
    afterModal = "##sellall"
  end)
  if W.pressed("rt") and #rows > 0 then nms.loot_send(LOOT_ALL) end
end

-- ---------------------------------------------------------------- Rules
local function rulesTab(s, x0, y0, x1, y1)
  local wx, wy = ui.GetWindowPos()
  local gap, bh = T.px(12), T.px(72)
  ui.SetCursorPos(x0 - wx, y0 - wy)
  CH.edit(st, "lootsearch", "Search", search, function(t) search = t end, T.px(420), bh, { max = 64 })
  ui.SameLine(0, gap)
  if T.button("##rclear", "Clear", T.px(140), bh, { disabled = search == "" }, pad) then search = "" end
  ui.SameLine(0, gap)
  CH.refocus(st, "sort")
  if T.button("##rsort", "Order", T.px(240), bh, { sub = SORTS[sortBy], align = 0 }, pad) then
    CH.choose(st, "Order the rules by", SORTS, sortBy, function(i) sortBy = i end, "sort")
  end
  ui.SameLine(0, gap)
  if T.button("##rreload", "Reload file", T.px(220), bh, { sub = (s.rulesFile:match("([^/\\]+)$") or "?"), align = 0 }, pad) then
    nms.loot_window("reload")
  end
  local counts = { [0] = 0, 0, 0, 0, 0 }
  for _, r in ipairs(s.rules) do counts[r.type] = (counts[r.type] or 0) + 1 end
  T.font(19)
  T.textIn(x0, y0, x1, y0 + bh, string.format("%d keep / %d sell / %d once", counts[ALWAYS_KEEP], counts[ALWAYS_SELL],
    counts[KEEP_ONCE] + counts[SELL_ONCE]), C.dim, 1)
  T.unfont()
  -- the rules, filtered and ordered
  local q = search:lower()
  local list = {}
  for _, r in ipairs(s.rules) do
    if q == "" or r.name:lower():find(q, 1, true) or tostring(r.id):find(q, 1, true) then list[#list + 1] = r end
  end
  table.sort(list, function(a, b)
    local x, y
    if sortBy == 2 then x, y = a.id, b.id
    elseif sortBy == 3 then x, y = a.type, b.type
    else x, y = a.name:lower(), b.name:lower() end
    if x == y then return a.id < b.id end
    return x < y
  end)
  local ly0 = y0 + bh + T.px(16)
  local ly1 = y1 - bh - T.px(16)
  local rowH = 58
  local visible = math.max(1, math.floor((ly1 - ly0 + T.px(10)) / (T.px(rowH) + T.px(10))))
  clampFocus(rules, #list, visible)
  ui.SetCursorPos(x0 - wx, ly0 - wy)
  local f
  if #list == 0 then
    T.font(21)
    T.text(x0 + T.px(6), ly0 + T.px(10), #s.rules == 0 and "No rules yet. Y on a drop sets one." or "No rule matches the search.", C.faint)
    T.unfont()
    ui.Dummy(x1 - x0, T.px(rowH))
  else
    f = W.list("nmsrules", rules, #list, visible, rowH, x1 - x0, function(i, rx0, ry0, rx1, ry1)
      local r = list[i]
      T.font(21)
      T.textIn(rx0 + T.px(18), ry0, rx0 + T.px(220), ry1, RULE_LABEL[r.type] or "?", ruleColor(r.type), 0)
      T.textIn(rx0 + T.px(230), ry0, rx0 + T.px(360), ry1, tostring(r.id), C.dim, 0)
      T.textIn(rx0 + T.px(370), ry0, rx1 - T.px(18), ry1, T.fit(r.name ~= "" and r.name or "(unknown name)", rx1 - rx0 - T.px(390)),
        r.name ~= "" and C.text or C.faint, 0)
      T.unfont()
    end, function(i)
      local r = list[i]
      typeMenu("Rule for " .. (r.name ~= "" and r.name or tostring(r.id)), r.type, function(t) nms.loot_rule(r.id, "", t) end)
      rowModal = rules
    end)
  end
  holdFocus(rules, f, #list, visible)
  if f then
    local r = list[f]
    if W.pressed("x") then nms.loot_rule(r.id, "", NONE) end
    if W.pressed("y") then nms.loot_send(INSPECT_ITEM, 0, r.id) end
  end
  -- add a rule
  ui.SetCursorPos(x0 - wx, y1 - bh - wy)
  CH.edit(st, "lootaddid", "Item ID", addId, function(t) addId = t:gsub("%D", "") end, T.px(300), bh, { max = 9 })
  ui.SameLine(0, gap)
  CH.refocus(st, "addtype")
  if T.button("##addtype", "Type", T.px(260), bh, { sub = RULE_LABEL[addType], align = 0 }, pad) then
    typeMenu("The new rule's type", addType, function(t) if t ~= NONE then addType = t end end, "addtype")
  end
  ui.SameLine(0, gap)
  local id = tonumber(addId) or 0
  if T.button("##addrule", "Add rule", T.px(220), bh, { primary = true, disabled = id <= 0 }, pad) then
    nms.loot_rule(id, "", addType)
    addId = ""
  end
  T.font(18)
  T.textIn(x0, y1 - bh, x1, y1, "A: change   X: remove   Y: inspect", C.faint, 1)
  T.unfont()
end

-- ---------------------------------------------------------------- History
local function ago(age)
  if age < 60 then return age .. "s ago" elseif age < 3600 then return (age // 60) .. "m ago" end
  return (age // 3600) .. "h ago"
end

local function historyTab(s, x0, y0, x1, y1)
  local wx, wy = ui.GetWindowPos()
  local gap, bh = T.px(12), T.px(64)
  ui.SetCursorPos(x0 - wx, y0 - wy)
  if T.button("##hrefresh", "Refresh", T.px(200), bh, {}, pad) then nms.loot_send(HISTORY) end
  ui.SameLine(0, gap)
  if T.button("##hclear", "Clear", T.px(200), bh, {}, pad) then nms.loot_window("clearHistory") end
  T.font(19)
  T.textIn(x0 + T.px(440), y0, x1, y0 + bh, "Your rules' and the auto-sell threshold's, this session.", C.dim, 1)
  T.unfont()
  -- the rules' own actions first (newest at the top), then what the server resolved on its own
  local list = {}
  for _, h in ipairs(s.localHistory) do
    local kept = h.rule == ALWAYS_KEEP or h.rule == KEEP_ONCE
    list[#list + 1] = { what = (kept and "Kept" or "Sold") .. " (" .. (RULE_LABEL[h.rule] or "?") .. ")", c = ruleColor(h.rule),
      name = h.name, qty = h.qty, npc = h.npc, copper = 0, age = h.age }
  end
  for _, h in ipairs(s.history) do
    local what, c = "Destroyed", C.faint
    if h.action == 1 then what, c = "Kept (server)", C.good elseif h.action == 2 then what, c = "Sold (threshold)", C.gold end
    list[#list + 1] = { what = what, c = c, name = h.name, qty = h.qty, npc = h.npc, copper = h.copper, age = h.age,
      icon = h.icon, item = h.item }
  end
  local ly0 = y0 + bh + T.px(16)
  local rowH = 58
  local visible = math.max(1, math.floor((y1 - ly0 + T.px(10)) / (T.px(rowH) + T.px(10))))
  ui.SetCursorPos(x0 - wx, ly0 - wy)
  if #list == 0 then
    T.font(21)
    T.text(x0 + T.px(6), ly0 + T.px(10), "Nothing yet.", C.faint)
    T.unfont()
    return
  end
  local function inspect(h) if h.item then nms.loot_send(INSPECT_ITEM, 0, h.item) end end
  local f = W.list("nmshist", hist, #list, visible, rowH, x1 - x0, function(i, rx0, ry0, rx1, ry1)
    local h = list[i]
    local s_ = ry1 - ry0 - T.px(14)
    if h.icon then T.itemIcon(h.icon, rx0 + T.px(10), ry0 + T.px(7), rx0 + T.px(10) + s_, ry0 + T.px(7) + s_) end
    local x = rx0 + s_ + T.px(24)
    local w = rx1 - x - T.px(18)
    T.font(20)
    T.textIn(x, ry0, x + w * 0.24, ry1, T.fit(h.what, w * 0.24 - T.px(8)), h.c, 0)
    T.textIn(x + w * 0.24, ry0, x + w * 0.62, ry1, T.fit(h.qty > 1 and (h.name .. " x" .. h.qty) or h.name, w * 0.38 - T.px(8)), C.text, 0)
    T.textIn(x + w * 0.62, ry0, x + w * 0.80, ry1, T.fit(h.npc, w * 0.18 - T.px(8)), C.dim, 0)
    T.textIn(x + w * 0.80, ry0, x + w * 0.91, ry1, h.copper > 0 and money(h.copper) or "-", h.copper > 0 and C.gold or C.faint, 0)
    T.textIn(x + w * 0.91, ry0, x + w, ry1, ago(h.age), C.dim, 1)
    T.unfont()
  end, function(i) inspect(list[i]) end)
  holdFocus(hist, f, #list, visible)
  if f and W.pressed("y") then inspect(list[f]) end
end

-- ---------------------------------------------------------------- Settings
local function settingsTab(s, x0, y0, x1, y1)
  local wx, wy = ui.GetWindowPos()
  local gap, bh = T.px(12), T.px(76)
  local half = (x1 - x0 - T.px(40)) / 2
  local rx0 = x0 + half + T.px(40)
  -- the server's
  CH.caption(x0, y0, "Server (saved on your character)")
  local y = y0 + T.px(30)
  ui.SetCursorPos(x0 - wx, y - wy)
  if not s.settings then
    T.font(21)
    T.text(x0, y, "Waiting for the server...", C.dim)
    T.unfont()
    ui.SetCursorPos(x0 - wx, y + T.px(40) - wy)
    if T.button("##request", "Request", half, bh, {}, pad) then nms.loot_send(GET_SETTINGS) end
  else
    local se = s.settings
    if not se.enabled then
      T.font(19)
      T.text(x0, y, "Personal Loot is disabled on this server.", C.bad)
      T.unfont()
      y = y + T.px(34)
      ui.SetCursorPos(x0 - wx, y - wy)
    end
    if toggle("##defer", "Hold loot until I'm out of combat", se.defer, "the window waits for no aggro", half, bh) then
      nms.loot_send(SET_DEFER, 0, 0, se.defer and 0 or 1)
    end
    if toggle("##report", "Chat summary of auto-sold items", se.report, "one line per kill", half, bh) then
      nms.loot_send(SET_REPORT, 0, 0, se.report and 0 or 1)
    end
    local text = sellUnder or tostring(se.sellUnder)
    local v = math.max(0, tonumber(text) or 0)
    CH.edit(st, "sellunder", "Auto-sell unknown items worth less than (copper)", text,
      function(t) sellUnder = t:gsub("%D", "") end, half, bh, { max = 9 })
    if T.button("##apply", "Apply", half, T.px(64), { primary = sellUnder ~= nil and v ~= se.sellUnder,
          sub = "= " .. (v > 0 and money(v) or "off (0)"), align = 0 }, pad) then
      nms.loot_send(SET_SELL_UNDER, 0, 0, v)
      sellUnder = nil
    end
    local _, cy = ui.GetCursorScreenPos()
    T.font(18)
    T.text(x0, cy, "The threshold works on the server, before a drop reaches this window.", C.faint)
    T.text(x0, cy + T.px(26), "Undecided drops are looted for you after " .. se.timeout .. " seconds.", C.faint)
    T.unfont()
  end
  -- the window's own
  CH.caption(rx0, y0, "Window (this computer)")
  local wy0 = y0 + T.px(30)
  local pbh = T.px(68)
  for i, e in ipairs(PREFS) do
    local key = "nms.loot." .. e[2]
    local cur = ui.GetSetting(key)
    local on = (cur == nil and e[3]) or cur == "1"
    ui.SetCursorPos(rx0 - wx, wy0 + (i - 1) * (pbh + gap) - wy)
    if toggle("##pref" .. i, e[4], on, e[5], x1 - rx0, pbh) then ui.SetSetting(key, on and "0" or "1") end
  end
  T.font(17)
  local fy = y1 - T.px(78)
  T.text(x0, fy, T.fit("Rules file: " .. (s.rulesFile ~= "" and s.rulesFile or "(unknown until in game)"), x1 - x0), C.faint)
  T.text(x0, fy + T.px(26), "Commands: /loot [show|hide|toggle|pin|lock|rule <id> <keep|sell|keeponce|sellonce|none>|reset]", C.faint)
  T.text(x0, fy + T.px(52), "Server: #advloot, #lootpending, #loottriage, #lootdefer, #lootreport, #lootsellunder", C.faint)
  T.unfont()
end

-- ---------------------------------------------------------------- the window
local TABS = { "Loot", "Rules", "History", "Settings" }
local HINTS = {
  { { "a", "Keep" }, { "x", "Sell" }, { "y", "Rules, inspect" }, { "rt", "Loot all" }, { "lb", "Tab" }, { "rb", "Tab" }, { "b", "Close" } },
  { { "a", "Change" }, { "x", "Remove" }, { "y", "Inspect" }, { "lb", "Tab" }, { "rb", "Tab" }, { "b", "Close" } },
  { { "a", "Inspect" }, { "lb", "Tab" }, { "rb", "Tab" }, { "b", "Close" } },
  { { "a", "Change" }, { "lb", "Tab" }, { "rb", "Tab" }, { "b", "Close" } },
}

ui.window(NAME, function()
  if not eq.InGame() or not nms then return end
  local s = nms.loot()
  if not s then return end
  eq.ReplaceWindow("NmsLootWnd") -- (this window stands in for the module's own C++ one)
  local pr = prefs()
  if s.tab and s.tab >= 0 then page = s.tab + 1 end
  if not s.visible then
    wasShown, st.kb.open, st.pick, ask.confirm, wasBusy, rowModal, afterModal = false, false, nil, nil, false, nil, nil
    return
  end
  T.frame()
  local appearing = not wasShown
  wasShown = true
  if appearing then page = (s.tab and s.tab >= 0) and s.tab + 1 or (#s.rows > 0 and 1 or page) end
  local n = #s.rows
  local sub = n == 0 and "Nothing waiting" or (n == 1 and "1 drop waiting for a decision" or (n .. " drops waiting for a decision"))
  if s.settings and s.settings.defer then sub = sub .. "   |   held in combat" end
  local hints = ask.confirm and { { "a", "Choose" }, { "b", "Back" } } or CH.hints(st, HINTS[page])
  if W.begin(NAME, { w = 1640, h = 960, title = n > 0 and (NAME .. " (" .. n .. ")") or NAME, subtitle = sub,
        hints = hints, alwaysFocus = true }) then
    local x0, y0, x1, y1 = W.area()
    local busy = ask.confirm ~= nil or CH.busy(st)
    if wasBusy and not busy then -- (a picker or the question went: the focus back where it was)
      if rowModal and rowModal.v then rowModal.focusNext = rowModal.v
      elseif afterModal then focusBtn = afterModal end
      rowModal, afterModal = nil, nil
    end
    wasBusy = busy
    if not W.confirm(ask) and not CH.modal(st, x0, y0, x1, y1) then
      local step = (M.pressed(NAME, "rb") and 1 or 0) - (M.pressed(NAME, "lb") and 1 or 0)
      if step ~= 0 then page = (page - 1 + step) % #TABS + 1 end
      local top = M.tabs(TABS, page, x0, y0, function(i) page = i end)
      if page == 1 then lootTab(s, pr, x0, top, x1, y1, appearing)
      elseif page == 2 then rulesTab(s, x0, top, x1, y1)
      elseif page == 3 then historyTab(s, x0, top, x1, y1)
      else settingsTab(s, x0, top, x1, y1) end
    end
  end
  local close = W.finish()
  if close and ask.confirm then
    ask.confirm = nil
  elseif CH.back(st, close) then
    nms.loot_window("show", false)
  end
end)
