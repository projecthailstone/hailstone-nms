-- Pet (NMS): covers the pack's Pet window on an NMS server. The pack's window (lib/pet.lua) with the server's
-- #petcmd added above the stock command buttons, which fold under "Pet commands" (folded at first): the pets to
-- command (All, the classes that summoned the pets OP_PetList lists, or Swarm) and a button for each of the server's
-- commands, On / Off for its switches. The server does not say which switches are on, except for swarm pets (asked
-- once a zone): those light. On another server, or before the plugin has seen this one, the pack's own window
-- draws (ui.base_window).
local pet = dofile("lib/pet.lua")
local hud = dofile("lib/hud.lua")
local T = hud.T
local C = T.color
local nms = eq.Plugin("nms")

local TARGETS_PER_ROW, VERBS_PER_ROW, TOGGLES_PER_ROW = 4, 4, 2
local ROW = 26
local target = "all"

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

local function split(st)
  local simple, toggles = {}, {}
  for _, v in ipairs(st.verbs) do
    if v.toggle then toggles[#toggles + 1] = v else simple[#simple + 1] = v end
  end
  return simple, toggles
end

local function height(st, list)
  local simple, toggles = split(st)
  return 18 + math.ceil(#list / TARGETS_PER_ROW) * ROW + math.ceil(#simple / VERBS_PER_ROW) * ROW
    + math.ceil(#toggles / TOGGLES_PER_ROW) * ROW + 6
end

local function draw(st, list, x, y, w, s)
  local pad = 9 * s
  local gap = 4 * s
  local inner = w - 2 * pad
  T.Text(x + pad, y + 2 * s, "Command pets", C.muted, T.font.tiny)
  y = y + 18 * s
  -- the pets to command
  local found = false
  for _, t in ipairs(list) do found = found or t.word == target end
  if not found then target = "all" end -- the class's pets went
  local tw = (inner - (TARGETS_PER_ROW - 1) * gap) / TARGETS_PER_ROW
  for k, t in ipairs(list) do
    local col, row = (k - 1) % TARGETS_PER_ROW, (k - 1) // TARGETS_PER_ROW
    local bx, by = x + pad + col * (tw + gap), y + row * ROW * s
    if T.Button("##nmspt" .. t.word, bx, by, bx + tw, by + 22 * s, s, T.Fit(t.label, tw - 6 * s, T.font.small),
        { primary = t.word == target, tooltip = t.word == "all" and "All your pets" or (t.label .. "'s pets") }) then
      target = t.word
    end
  end
  y = y + math.ceil(#list / TARGETS_PER_ROW) * ROW * s
  local swarm = target == "swarm"
  if swarm then nms.pet_swarm_query() end
  local simple, toggles = split(st)
  -- the commands
  local vw = (inner - (VERBS_PER_ROW - 1) * gap) / VERBS_PER_ROW
  for k, v in ipairs(simple) do
    local col, row = (k - 1) % VERBS_PER_ROW, (k - 1) // VERBS_PER_ROW
    local bx, by = x + pad + col * (vw + gap), y + row * ROW * s
    local ok = not swarm or v.swarm
    if T.Button("##nmspv" .. v.word, bx, by, bx + vw, by + 22 * s, s, T.Fit(v.label, vw - 6 * s, T.font.small),
        { enabled = ok, tooltip = ok and ("#petcmd " .. v.word .. " " .. target) or "Swarm pets do not take this" }) then
      nms.pet_command(v.word, "", target)
    end
  end
  y = y + math.ceil(#simple / VERBS_PER_ROW) * ROW * s
  -- the switches: the name, On, Off
  local cw = (inner - (TOGGLES_PER_ROW - 1) * gap) / TOGGLES_PER_ROW
  local bw = 28 * s
  for k, v in ipairs(toggles) do
    local col, row = (k - 1) % TOGGLES_PER_ROW, (k - 1) // TOGGLES_PER_ROW
    local cx, cy = x + pad + col * (cw + gap), y + row * ROW * s
    local ok = not swarm or v.swarm
    local known = swarm and st.swarm[v.word]
    local _, th = T.TextSize("Ag", T.font.tiny)
    T.Text(cx, cy + (22 * s - th) / 2, T.Fit(v.label, cw - 2 * bw - 8 * s, T.font.tiny), ok and C.text or C.faint, T.font.tiny)
    for i, state in ipairs({ "on", "off" }) do
      local bx = cx + cw - (3 - i) * bw - (2 - i) * 2 * s
      local lit = known ~= nil and (known == (state == "on"))
      if T.Button("##nmsps" .. v.word .. state, bx, cy, bx + bw, cy + 22 * s, s, state == "on" and "On" or "Off",
          { enabled = ok, primary = lit,
            tooltip = ok and ("#petcmd " .. v.word .. " " .. state .. " " .. target) or "Swarm pets do not take this" }) then
        nms.pet_command(v.word, state, target)
      end
    end
  end
  return y + math.ceil(#toggles / TOGGLES_PER_ROW) * ROW * s + 6 * s
end

ui.window("Pet", function()
  local st = nms and nms.pets()
  if not st then return ui.base_window() end
  local list = targets(st)
  pet.window({
    width = 300,
    fold = "nms.pet.commandsOpen",
    extra = { height = function() return height(st, list) end, draw = function(x, y, w, s) return draw(st, list, x, y, w, s) end },
  })
end)
