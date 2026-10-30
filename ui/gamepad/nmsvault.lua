-- Dimensional Vault (gamepad pack): this server's vault (the shipped NMS patch's Dimensional Pocket Vault,
-- plugins/nms/nms_vault.h; nms.vault), opened by the server's VAULTDATA|OPEN line, with every function of the Modern
-- UI's (plugins/nms/ui/modern/lib/nmsvault.lua):
--   the nine pages as tabs (LB / RB; the current one lit), the Clicky / Proc Locker note, the page's slots (Pri / Sec /
--   Rng on the Proc Locker). On a slot A deposits the item you hold or withdraws the slot's, X withdraws one of a
--   stack (the patch's Ctrl-click), Y opens a bag's window (its contents here, below the slots; one bag at a time) or
--   inspects an item. In the open bag the same: A deposits into that bag slot or withdraws, X one of a stack, Y
--   inspects. The focused slot is described (a bag's contents listed, as the patch's tooltip).
--   On the right Bank and Merchant, and your bags: A picks an item up to deposit (lib/exchange.lua), X puts it back.
-- The mouse: a left click is A, a right click Y. Every button says the patch's "#vault_*" command (nms.vault_say).
-- B hides it.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local X = dofile("lib/exchange.lua")
local M = dofile("lib/markets.lua")
local CH = dofile("lib/child.lua")
local G = dofile("lib/stock.lua")
local C = T.C
local NAME = "Dimensional Vault"

local nav, bags = {}, {}
local wasShown = false
local focusSlot   -- a vault slot to take the pad's focus (a new page: its first)
local lastPage

local function holdingHint(v)
  return v.cursor and ("Holding " .. v.cursor.name .. ": A on a slot deposits it") or nil
end

-- A vault slot as T.slot's item.
local function asItem(s)
  if not s or (s.item or 0) == 0 then return nil end
  return { icon = s.icon, name = s.name }
end

-- The focused slot's description in x0, y .. x1: its item (a bag's contents listed), or what A does on it.
local function describe(v, s, where, x0, y, x1, y1)
  CH.caption(x0, y, where)
  y = y + T.px(26)
  if not s then
    T.font(21)
    T.text(x0, y, v.cursor and ("Empty. A deposits " .. v.cursor.name .. " here.") or "Empty.", v.cursor and C.accent or C.faint)
    T.unfont()
    return
  end
  T.font(24)
  T.text(x0, y, T.fit(s.name or "", x1 - x0), C.text)
  T.unfont()
  y = y + T.px(36)
  T.font(19)
  if (s.bag or 0) > 0 then
    T.text(x0, y, string.format("A bag of %d slots%s", s.bag, #s.contents > 0 and ", holding:" or ", empty"), C.dim)
    y = y + T.px(28)
    local cols = 2
    local cw = (x1 - x0) / cols
    for i, c in ipairs(s.contents) do
      local cx = x0 + ((i - 1) % cols) * cw
      local cy = y + ((i - 1) // cols) * T.px(30)
      if cy + T.px(28) > y1 then break end
      T.itemIcon(c.icon, cx, cy, cx + T.px(26), cy + T.px(26))
      T.text(cx + T.px(34), cy, T.fit(c.name .. (c.qty >= 2 and (" x" .. c.qty) or ""), cw - T.px(44)), C.text)
    end
  else
    T.text(x0, y, "A withdraws it, X one of a stack, Y inspects it.", C.dim)
  end
  T.unfont()
end

ui.window(NAME, function()
  if not eq.InGame() or not nms then return end
  local v = nms.vault()
  if not v or not v.shown then
    wasShown = false
    return
  end
  T.frame()
  local appearing = not wasShown
  wasShown = true
  local ctx = X.frame(nav, {})
  local hints = { { "a", v.cursor and "Deposit" or "Withdraw / pick up" }, { "x", "Withdraw one" },
    { "y", "Open bag / inspect" }, { "lb", "Page" }, { "rb", "Page" }, { "b", "Close" } }
  if W.begin(NAME, { w = 1660, h = 980, title = NAME, subtitle = holdingHint(v) or "Nine pages of storage that go where you go",
        hints = hints, alwaysFocus = true }) then
    local x0, y0, x1, y1 = W.area()
    local wx, wy = ui.GetWindowPos()
    -- the pages
    local step = (M.pressed(NAME, "rb") and 1 or 0) - (M.pressed(NAME, "lb") and 1 or 0)
    if step ~= 0 then nms.vault_say("#vault_page " .. ((v.page - 1 + step) % 9 + 1)) end
    local top = M.tabs(v.labels, v.page, x0, y0, function(p) nms.vault_say("#vault_page " .. p) end)
    -- (the server answers a page change with the new page: its first slot takes the focus)
    if appearing or v.page ~= lastPage then focusSlot = v.first end
    lastPage = v.page
    local lx1 = x0 + T.px(820)
    local y = top
    if v.page >= 7 then
      local note = v.page == 9
        and "Place a weapon in the first slot here to use its proc (does not stack with augment procs). Place a shield in the second slot to gain its stats & passive shielding block rate!"
        or "Clicky buff bags placed in these pages will automatically be loaded upon zoning or relogging."
      T.font(19)
      y = G.wrapped(note, x0, y, lx1 - x0, C.gold, 3) + T.px(12)
      T.unfont()
    end
    -- the page's slots
    local size, gap, per = T.px(110), T.px(12), 5
    local focused, focusedWhere
    for slot = v.first, v.last do
      local k = slot - v.first
      if k % per == 0 then ui.SetCursorPos(x0 - wx, y + (k // per) * (size + gap) - wy) else ui.SameLine(0, gap) end
      local s = v.slots[slot]
      local label = v.page == 9 and (({ "Pri", "Sec" })[k + 1] or "Rng") or tostring(k + 1)
      if focusSlot == slot then
        ui.SetKeyboardFocusHere()
        pad.showCursor()
        focusSlot = nil
      end
      local open = false
      for _, b in ipairs(v.bags) do if b.slot == slot then open = true end end
      local clicked, isFocused = T.slot("##vault" .. slot, asItem(s), size, { label = label, selected = open }, pad)
      local function primary()
        if v.cursor then nms.vault_say("#vault_deposit " .. slot)
        elseif s then nms.vault_say("#vault_withdraw " .. slot) end
      end
      local function secondary()
        if not s then return end
        if (s.bag or 0) < 1 then
          nms.vault_window("inspect", slot)
        else
          for _, b in ipairs(v.bags) do if b.slot ~= slot then nms.vault_window("closeBag", b.slot) end end
          nms.vault_window("bag", slot)
        end
      end
      if clicked then primary() end
      if ui.IsItemClicked(ui.Mouse.Right) then secondary() end
      if isFocused then
        focused, focusedWhere = s or false, (v.page == 9 and label or ("Slot " .. label)) .. " of " .. v.labels[v.page]
        if W.pressed("x") and s then nms.vault_say("#vault_withdraw " .. slot .. " 1") end
        if W.pressed("y") then secondary() end
      end
    end
    local rows = math.ceil((v.last - v.first + 1) / per)
    y = y + rows * (size + gap) + T.px(8)
    -- the open bag
    local bag = v.bags[1]
    local bagFocus
    if bag then
      ui.DrawLine(x0, y, lx1, y, C.ink[1], C.ink[2], C.ink[3], 0.08, 1)
      y = y + T.px(10)
      CH.caption(x0, y, bag.name .. " (vault slot " .. (bag.slot - v.first + 1) .. ")")
      y = y + T.px(28)
      local bs, bper = T.px(76), 9
      for i = 0, bag.size - 1 do
        if i % bper == 0 then ui.SetCursorPos(x0 - wx, y + (i // bper) * (bs + T.px(8)) - wy) else ui.SameLine(0, T.px(8)) end
        local item
        for _, c in ipairs(bag.contents) do if c.slot == i then item = c break end end
        local clicked, isFocused = T.slot("##vbag" .. bag.slot .. "_" .. i, item and { icon = item.icon, count = item.qty,
          stackable = item.qty >= 2 } or nil, bs, { label = tostring(i + 1) }, pad)
        if clicked then
          if v.cursor then nms.vault_say("#vault_deposit_bagitem_specific " .. bag.slot .. " " .. i)
          elseif item then nms.vault_say("#vault_withdraw_bagitem " .. bag.slot .. " " .. item.slot) end
        end
        if ui.IsItemClicked(ui.Mouse.Right) and item then nms.vault_window("inspectItem", item.item) end
        if isFocused then
          bagFocus = { item = item, i = i }
          if W.pressed("x") and item then nms.vault_say("#vault_withdraw_bagitem " .. bag.slot .. " " .. item.slot .. " 1") end
          if W.pressed("y") and item then nms.vault_window("inspectItem", item.item) end
        end
      end
      y = y + math.ceil(bag.size / bper) * (bs + T.px(8)) + T.px(4)
      ui.SetCursorPos(x0 - wx, y - wy)
      if T.button("##closebag", "Close " .. bag.name, T.px(320), T.px(56), {}, pad) then nms.vault_window("closeBag", bag.slot) end
      y = y + T.px(56) + T.px(12)
    end
    -- what the focus is on
    if bagFocus then
      local it = bagFocus.item
      CH.caption(x0, y, bag.name .. ", slot " .. (bagFocus.i + 1))
      T.font(22)
      T.text(x0, y + T.px(26), it and T.fit(it.name .. (it.qty >= 2 and (" (x" .. it.qty .. ")") or ""), lx1 - x0)
        or (v.cursor and ("Empty. A deposits " .. v.cursor.name .. " here.") or "Empty."), it and C.text or (v.cursor and C.accent or C.faint))
      T.unfont()
    elseif focused ~= nil then
      describe(v, focused or nil, focusedWhere, x0, y, lx1, y1)
    end
    -- the right: Bank, Merchant and your bags
    local rx0 = lx1 + T.px(40)
    local bh = T.px(64)
    ui.SetCursorPos(rx0 - wx, top - wy)
    local bw = (x1 - rx0 - T.px(12)) / 2
    if T.button("##vbank", "Bank", bw, bh, { sub = "Open your bank", align = 0 }, pad) then nms.vault_say("#vault_bank") end
    ui.SameLine(0, T.px(12))
    if T.button("##vmerchant", "Merchant", bw, bh, { sub = "Sell from anywhere", align = 0 }, pad) then nms.vault_say("#vault_merchant") end
    ui.SetCursorPos(rx0 - wx, top + bh + T.px(20) - wy)
    local bper = math.max(1, math.floor((x1 - rx0 + T.px(X.GAP)) / T.px(X.SIZE + X.GAP)))
    X.bags(ctx, bags, rx0, x1 - rx0, bper)
    X.holding(ctx, rx0, y1 - X.holdingHeight(ctx), x1, "A on a vault slot deposits it; X puts it back in your bags")
  end
  if W.finish() then nms.vault_window("hide") end
end)
