-- Pet (NMS, gamepad pack): covers the pack's Pet window on an NMS server. The pack's pieces (lib/pet.lua) in three
-- columns: the pets' HP on the left, the commands in the middle, the buffs on the right. The middle shows the
-- server's #petcmd ("Command pets") or the stock command buttons ("Pet commands"), one at a time (the stock ones
-- rolled up at first):
--   the pets to command: All, the classes that summoned the pets OP_PetList lists, or Swarm
--   a button for each of the server's commands (A sends "#petcmd <command> <pets>")
--   its switches (taunt, hold, ...): "Turn on" / "Turn off" picks what A sends. The server does not say which are
--   on, except for swarm pets (asked once a zone): theirs show ON / OFF.
-- On another server, or before the plugin has seen this one, the pack's own window draws (ui.base_window).
local P = dofile("lib/pet.lua")
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local AS = dofile("lib/assign.lua")
local C = T.C
local nms = eq.Plugin("nms")

local target = "all"
local switchOn = true
local HINTS = { { "a", "Command" }, { "b", "Close" } }

local function targets(st)
  local list = { { word = "all", label = "All" } }
  local seen = {}
  for _, p in ipairs(st.pets) do
    if p.word ~= "" and not seen[p.word] then
      seen[p.word] = true
      list[#list + 1] = { word = p.word, label = p.className }
    end
  end
  list[#list + 1] = { word = "swarm", label = "Swarm" }
  return list
end

-- A row of buttons, `cols` across, from the cursor: draw(k, item, width) per item.
local function grid(items, width, cols, draw)
  local gap = T.px(10)
  local bw = (width - (cols - 1) * gap) / cols
  for k, item in ipairs(items) do
    if (k - 1) % cols ~= 0 then ui.SameLine(0, gap) end
    draw(k, item, bw)
  end
end

local function petcmd(st, x0, y, width)
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x0 - wx, y - wy)
  local list = targets(st)
  local found = false
  for _, t in ipairs(list) do found = found or t.word == target end
  if not found then target = "all" end -- the class's pets went
  T.heading("Pets", width)
  grid(list, width, 4, function(_, t, bw)
    if T.button("##nmspt" .. t.word, t.label, bw, T.px(56), { primary = t.word == target }, pad) then target = t.word end
  end)
  local swarm = target == "swarm"
  if swarm then nms.pet_swarm_query() end
  local simple, toggles = {}, {}
  for _, v in ipairs(st.verbs) do
    if v.toggle then toggles[#toggles + 1] = v else simple[#simple + 1] = v end
  end
  T.heading("Commands", width)
  grid(simple, width, 4, function(_, v, bw)
    if T.button("##nmspv" .. v.word, v.label, bw, T.px(56), { disabled = swarm and not v.swarm }, pad)
        and not (swarm and not v.swarm) then
      nms.pet_command(v.word, "", target)
    end
  end)
  T.heading("Switches", width)
  local modes = { { on = true, label = "Turn on" }, { on = false, label = "Turn off" } }
  grid(modes, width, 2, function(_, m, bw)
    if T.button("##nmspm" .. m.label, m.label, bw, T.px(56), { primary = switchOn == m.on }, pad) then switchOn = m.on end
  end)
  grid(toggles, width, 4, function(_, v, bw)
    local ok = not swarm or v.swarm
    local clicked, _, cx0, cy0, cx1 = T.button("##nmsps" .. v.word, v.label, bw, T.px(56), { disabled = not ok }, pad)
    local known = swarm and st.swarm[v.word]
    if known ~= nil then
      T.font(15)
      T.textIn(cx0, cy0 + T.px(4), cx1 - T.px(10), cy0 + T.px(22), known and "ON" or "OFF", C.dim, 1)
      T.unfont()
    end
    if clicked and ok then nms.pet_command(v.word, switchOn and "on" or "off", target) end
  end)
end

ui.window("Pet", function()
  local st = nms and nms.pets()
  if not st then return ui.base_window() end
  local open, name = P.visible()
  if not open then return end
  local stock = ui.GetSetting("nms.pet.stockCommands", "0") == "1"
  if W.begin("Pet", { w = 1600, h = 1000, title = "Pet", subtitle = name or "You have no pet.",
      hints = stock and P.HINTS or HINTS }) then
    local settled = AS.settled("Pet")
    local x0, y0, x1, y1 = W.area()
    local wx, wy = ui.GetWindowPos()
    local colW = T.px(500)
    P.status(x0, colW, name)
    local mx = x0 + colW + T.px(40)
    local midW = T.px(560)
    ui.SetCursorPos(mx - wx, y0 - wy)
    local sections = { { stock = false, label = "Command pets" }, { stock = true, label = "Pet commands" } }
    grid(sections, midW, 2, function(_, sec, bw)
      if T.button("##nmspsec" .. sec.label, sec.label, bw, T.px(56), { primary = stock == sec.stock }, pad) then
        stock = sec.stock
        ui.SetSetting("nms.pet.stockCommands", stock)
      end
    end)
    local _, cy = ui.GetCursorScreenPos()
    if stock then P.commands(mx, cy, midW, name, settled) else petcmd(st, mx, cy, midW) end
    P.buffs(mx + midW + T.px(40), y0, x1, y1)
  end
  if W.finish() then GPSTATE.petOpen = false end
end)
