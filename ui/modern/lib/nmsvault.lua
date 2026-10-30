-- The shipped NMS patch's Dimensional Pocket Vault (NMS/dinput8.dll FUN_1019a420, its bag windows FUN_1019b500 and
-- its contents tooltip FUN_1019bc60; plugins/nms/nms_vault.h): Bank and Merchant buttons, the nine page buttons
-- (the current one blue), the Clicky / Proc Locker note, and the page's slots as 40 x 40 cells (an empty one dark
-- with its number, "Pri" / "Sec" / "Rng" on the Proc Locker). A left click deposits the cursor's item into the slot or
-- withdraws the slot's item (with Ctrl: one of a stack); a right click inspects an item or opens a bag's own window.
-- Every button says the patch's "#vault_*" command (nms.vault_say). Shared by this pack's nmsvault.lua.
local nms = eq.Plugin("nms") -- this plugin's functions (plugins/nms/nms_ui.cpp)
local M = {}
local C = dofile("lib/theme.lua").color

local CELL = 40

local function icon(id, size)
  local tex, u0, v0, u1, v1 = eq.ItemIcon(id)
  if tex then ui.Image(tex, size, size, u0, v0, u1, v1) else ui.Dummy(size, size) end
end

-- FUN_1019bc60: a hovered bag's contents.
local function contentsTooltip(s)
  if #s.contents == 0 then return end
  ui.BeginTooltip()
  ui.PushTextWrapPos(370)
  ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, s.name .. " Contents:")
  ui.Separator()
  for _, c in ipairs(s.contents) do
    icon(c.icon, 24)
    ui.SameLine(0, -1)
    ui.Text(c.name)
    if c.qty >= 2 then
      ui.SameLine(0, -1)
      ui.TextColored(C.magic[1], C.magic[2], C.magic[3], 1, "x" .. c.qty)
    end
  end
  ui.PopTextWrapPos()
  ui.EndTooltip()
end

local function cursorHint(v)
  if v.cursor and ui.IsWindowHovered(ui.HoveredFlags.ChildWindows) then
    ui.BeginTooltip()
    icon(v.cursor.icon, 24)
    ui.SameLine(0, -1)
    ui.TextColored(C.pet[1], C.pet[2], C.pet[3], 1, "Left-Click empty slot to deposit " .. v.cursor.name)
    ui.EndTooltip()
  end
end

-- One 40 x 40 cell at the cursor: what is in it, or the empty frame with its label.
local function cell(id, label, labelColor)
  local x, y = ui.GetCursorScreenPos()
  if id and id > 0 then
    icon(id, CELL)
  else
    ui.DrawRectFilled(x, y, x + CELL, y + CELL, C.inset[1], C.inset[2], C.inset[3], 1)
    ui.DrawRect(x, y, x + CELL, y + CELL, C.frameBorder[1], C.frameBorder[2], C.frameBorder[3], 1, 0, 1)
    local tw, th = ui.CalcTextSize(label)
    ui.DrawText(x + (CELL - tw) / 2, y + (CELL - th) / 2, labelColor[1], labelColor[2], labelColor[3], 1, label)
    ui.Dummy(CELL, CELL)
  end
  ui.SetCursorScreenPos(x, y)
end

function M.Body(v)
  if ui.Button("Bank") then nms.vault_say("#vault_bank") end
  ui.SameLine(0, -1)
  if ui.Button("Merchant") then nms.vault_say("#vault_merchant") end
  ui.Separator()
  for p = 1, 9 do
    if p > 1 then ui.SameLine(0, -1) end
    local current = v.page == p
    if current then
      ui.PushStyleColor(ui.Col.Button, C.accent[1], C.accent[2], C.accent[3], 0.55)
      ui.PushStyleColor(ui.Col.ButtonHovered, C.accent[1], C.accent[2], C.accent[3], 0.70)
      ui.PushStyleColor(ui.Col.ButtonActive, C.accent[1], C.accent[2], C.accent[3], 0.85)
    end
    if ui.Button(v.labels[p]) then nms.vault_say("#vault_page " .. p) end
    if current then ui.PopStyleColor(3) end
  end
  ui.Separator()
  if v.page >= 7 then
    ui.PushTextWrapPos(0)
    if v.page == 9 then
      ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, "Place a weapon in the first slot here to use its proc (does not stack with augment procs). Place a shield in the second slot to gain its stats & passive shielding block rate!")
    else
      ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, "Clicky buff bags placed in these pages will automatically be loaded upon zoning or relogging.")
    end
    ui.PopTextWrapPos()
    ui.Separator()
  end
  local ctrl = ui.GetIO().ctrl
  -- the cells wrap where the next would pass the window's content edge
  local cx = ui.GetCursorScreenPos()
  local right = cx + ui.GetContentRegionAvail()
  local hovered
  for slot = v.first, v.last do
    ui.PushID(slot)
    local s = v.slots[slot]
    local label
    if v.page == 9 then
      label = ({ "Pri", "Sec" })[slot - v.first + 1] or "Rng"
    else
      label = tostring(slot - v.first + 1)
    end
    cell(s and s.icon, label, v.page == 9 and C.muted or C.faint)
    ui.InvisibleButton("##slot_btn", CELL, CELL)
    if ui.IsItemHovered() then
      if s then hovered = s end
    end
    if ui.IsItemClicked(ui.Mouse.Left) then
      if v.cursor then
        nms.vault_say("#vault_deposit " .. slot)
      elseif s then
        nms.vault_say("#vault_withdraw " .. slot .. (ctrl and " 1" or ""))
      end
    elseif ui.IsItemClicked(ui.Mouse.Right) and s then
      if s.bag < 1 then nms.vault_window("inspect", slot) else nms.vault_window("bag", slot) end
    end
    local _, _, x1 = ui.GetItemRect()
    if slot < v.last and x1 + 8 + CELL < right then ui.SameLine(0, -1) end
    ui.PopID()
  end
  if hovered then contentsTooltip(hovered)
  else cursorHint(v) end
end

-- FUN_1019b500: a bag's own window, 120 wide, two cells a row.
function M.Bag(v, b)
  ui.SetNextWindowSize(120, 0, ui.Cond.FirstUseEver)
  local visible, open = ui.Begin(b.name .. "##bag_" .. b.slot, true, ui.WindowFlags.AlwaysAutoResize)
  if visible then
    local ctrl = ui.GetIO().ctrl
    for i = 0, b.size - 1 do
      ui.PushID(i)
      local item
      for _, c in ipairs(b.contents) do
        if c.slot == i then item = c break end
      end
      if item then
        icon(item.icon, CELL)
      else
        ui.PushStyleColor(ui.Col.Button, C.frame[1], C.frame[2], C.frame[3], 1.0)
        ui.Button(tostring(i + 1), CELL, CELL)
        ui.PopStyleColor(1)
      end
      if ui.IsItemHovered() then
        if item then
          ui.BeginTooltip()
          ui.Text(item.qty >= 2 and string.format("%s (x%d)", item.name, item.qty) or item.name)
          ui.TextColored(C.hpLow[1], C.hpLow[2], C.hpLow[3], 1, "Left Click to withdraw item")
          ui.TextColored(C.hpLow[1], C.hpLow[2], C.hpLow[3], 1, "Right Click to inspect")
          ui.EndTooltip()
        elseif v.cursor then
          ui.BeginTooltip()
          ui.TextColored(C.pet[1], C.pet[2], C.pet[3], 1, "Deposit: " .. v.cursor.name)
          ui.TextColored(C.hpLow[1], C.hpLow[2], C.hpLow[3], 1, "Left Click to deposit here")
          ui.EndTooltip()
        end
      end
      if ui.IsItemClicked(ui.Mouse.Left) then
        if v.cursor then
          nms.vault_say("#vault_deposit_bagitem_specific " .. b.slot .. " " .. i)
        elseif item then
          nms.vault_say("#vault_withdraw_bagitem " .. b.slot .. " " .. item.slot .. (ctrl and " 1" or ""))
        end
      elseif ui.IsItemClicked(ui.Mouse.Right) and item then
        nms.vault_window("inspectItem", item.item)
      end
      if (i + 1) % 2 == 1 and i + 1 < b.size then ui.SameLine(0, -1) end
      ui.PopID()
    end
  end
  ui.End()
  if not open then nms.vault_window("closeBag", b.slot) end
end

return M
