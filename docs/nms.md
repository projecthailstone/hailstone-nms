# The NMS plugin

This repository is the NMS server's client changes as a Hailstone plugin. It does what the NMS patch
(`NMS/dinput8.dll`, which the NMS server ships with the game) does to the stock RoF2 client. It is built only
against the plugin SDK (`sdk/nmsc_plugin.h`, `sdk/nmsc_byte_io.h`), like any server's plugin, and the client has no
NMS code of its own. It is also the test case for the plugin interface: if a server's client needs something a plugin
cannot do, the interface is missing it (docs/research/plugin-v2-inventory.md).

The patch's behaviour comes from rof2decomp's static analysis of the DLL (`notes/nms-dinput8.md`, whose section
numbers the code cites) and from the NMS server's sources.

## Installing it

It is a Hailstone add-on (the client's docs/addons.md), released on this repository by `tools/release.sh` (README).
The client's own download does not include it. Built inside the client's tree it lands in
`build/hailstone/plugins/nms/` (`nms.so`, `nms.dll` or `nms.dylib`, with `ui/`), and
`cmake --install build --component nms --prefix <dir>` gives the same folder under `<dir>/plugins/nms/`.

- **A player** installs it from the Add-ons screen with this repository's link, or copies a package's folder into
  the Hailstone folder's `plugins/nms/`. The add-on asks for the capabilities below and gets no more. On a server
  without a `hailstone.cfg` (the NMS server today) a player's plugin may do everything, and `plugins.autoload = *`
  (the default) loads it at the world login.
- **A server** with Hailstone's script ships it in its `client_files/`: the binary for each platform in
  `plugins/<platform>/nms/` (`linux-x64`, `windows-x64`, `macos-arm64` ...) and the windows in `plugins/nms/ui/`.
  They land in `servers/<profile>/plugins/nms/`, and the server's copy wins over a player's.

It loads on any server and does nothing until it sees an NMS server. Project Triune runs a newer build of the same
patch and sends the same packets; it has its own plugin (`projecttriune`, kept in a private repository), and this one stands down on
a server whose saved name or world name contains a word of `skip_servers` (default `triune`): it then touches no
packet, so that plugin sees them.

## What it needs

On a server with a `hailstone.cfg` that does not ship it, the player's plugin capabilities must include the ones
below, or the matching part does not run (the chat window says which):

| Capability | For |
|---|---|
| `login` | loading at the world login: the characters' class masks at character select and the login checksum rewrite |
| `packets` | the NMS packets (OP_ServerAuthStats, OP_SendCharacterSets, OP_PetList, personal loot, waypoints ...), OP_CAuth and the 0x140f report |
| `extend` | the hooks, the rule switches, the class masks, the pet list and the server's vitals |
| `events` | the chat filter (the vault's lines, "Your song ends." without Bard) |
| `commands` | its commands |
| `chat` | the server's `#` commands it says for the player: the vault's and the pet window's buttons, `/alt toggle` |
| `move` | the scribe path's `/sit` |
| `ui` | its windows, its option and its menu entries |
| `actions`, `read` | the scribe path, the pet window's pet, the camera's zoom limit; reading the game state for the hooks |

## How it knows the server

It starts inactive (`set_active(0)`). The first of these turns it on for the rest of the login: the world's
OP_SendCharacterSets, the zone's OP_ServerAuthStats with a key in 1..0x83, or 0x140f from either. Before then the
client is the stock client. The only exception is OP_PetList, which the client used to read on any server: the
plugin passes it to the core whenever it comes.

## What it does

- **Classes**: the multiclass masks from OP_SendCharacterSets (by character, at character select) and
  OP_ServerAuthStats key 1 (the player's) go into the core, which shows them ("War/Clr"). The hooks follow the
  patch's detours: GetSpellLevelNeeded, IsSpellcaster, BardCastBard, CanUseMemorizedSpellSlot, the cast spoof, the
  skill gate and spoof class, the tracking rule, the guild class column (EQEmu's 1000 + mask), the best of the
  classes' HP / mana / endurance formulas, and the server's own "You begin casting" line.
- **Switches**: wide item links, every item usable (with a mask), Hide for any class, AAs from level 1 and Train
  All.
- **The server's values**: OP_ServerAuthStats keys 2..7 become the HP, mana and endurance the client answers and
  the player's current points; the patch's labels read the other keys.
- **Packets**: OP_CAuth at each zone-in (again every 8 s until the server echoes it), the 0x140f report once the
  mask came, and the login checksums with the patch's VM values (never real hardware ids) once the world sent
  OP_SendCharacterSets.
- **Discipline timers** from 20 up: the patch's band table and its own reuse map.
- **A spell scroll's Level Needed line**: the player's classes with their levels ("Level Needed: CLR(1) PAL(9)").
  When none of them can use the scroll, the classes that can follow ("Level Needed: CANNOT USE - CLR(1) PAL(9)";
  Hailstone's, the patch shows "CANNOT USE" alone).
- **The scribe path**: a right click on a scroll or tome scribes it (the book, the sit, the click again 100 ms
  later).
- **Several pets**: OP_PetList into the core (the pet window's gauges, the focused pet); `/pets`, `/petcycle`,
  `/petdebug`.
- **The extended target window**: the player's own pets that the server puts in its last slots are left out (the
  option "Hide My Pets in Extended Target", on by default; Hailstone's, not the patch's).
- **Chat**: `/who` in the patch's form; the vault's VAULTDATA lines and the patch's other lines hidden; OP_Weblink
  passed on: the core opens it as the stock client does (docs/browser.md), where the patch ignores it until
  `/browser on`.
- **Commands**: `/loot` (and `/nmsloot`), `/autoskill`, `/waypoint`, `/pets`, `/petcycle`, `/petdebug`, and
  `/alt toggle <id>` (the server's `#alttoggle`). Not the patch's `/fov` and `/camera` (the camera's zoom limit): the
  client's own options set the field of view and the zoom limit. `/autoskill <args>` goes to the server's
  `#autoskill`; `/autoskill` alone opens the client's Ability Book, where the patch toggled its Multiclass Autoplay
  Console. The console and its Autoplay Cooldowns window are left out on purpose: the Ability Book has autoskill.

## Its windows

`ui/modern/` and `ui/gamepad/` hold the Personal Loot window, the Dimensional Pocket Vault and the waypoint window ("Attune the Map") for each pack, and a Pet window. They use the pack's
own libraries (a plugin window's `dofile("lib/hud.lua")` finds the pack's), so they look like the rest of the pack. A
pack without them shows none of these windows.

**The Pet window** (Hailstone's; the patch has none for it) covers the pack's Pet window once the plugin has seen
the NMS server, and before that, or on another server, draws the pack's (docs/plugins.md, `ui.base_window`). It is
the pack's window (`lib/pet.lua`) with the server's `#petcmd` added (its `zone/gm_commands/petcmd.cpp`):
- the pets to command: All (the server's default), each class that summoned one of the pets OP_PetList lists
  (`mag`, `nec`, ...; a charmed pet has none), or Swarm;
- a button for each of the server's commands: attack, queue attack, follow, guard, sit, stop, back off, health,
  leader, feign death, dismiss (`leave`), and On / Off for its switches: taunt, hold, greater hold, spell hold,
  focus, regroup, assist. A button sends "/say #petcmd <command> [on|off] <pets>", and the echo of the line is
  hidden. Swarm pets do not take follow, guard, sit, health, feign, taunt, spell hold or regroup; those are off
  while Swarm is picked;
- the server tells the client no pet's switch states, so the switches are not lit, except for swarm pets: the
  server keeps their four (hold, greater hold, focus, assist) and reports them for "#petcmd hold ghold focus assist
  swarm", which the window sends (hidden, with its answers) the first time Swarm is picked in a zone;
- the stock command buttons stay, rolled up: the Modern window folds them under "Pet commands" (`nms.pet.commandsOpen`),
  the Gamepad window shows them in place of the `#petcmd` part ("Command pets" / "Pet commands",
  `nms.pet.stockCommands`). They send the stock OP_PetCommands to the pet in the pet window, and they are what makes
  hot buttons.

The server's `#petcmd` takes a class only as the pet's summoning class, so two pets of one class always take the same
command. The EQ menu's Plugins submenu and the gamepad pack's More page have
Personal Loot.

The windows reach the plugin through `eq.Plugin("nms")` (`plugins/nms/nms_ui.cpp` lists each function and what it
gives): `loot`, `loot_send`, `loot_rule`, `loot_window`, `vault`, `vault_say`, `vault_window`, `waypoints`, `waypoint_request`, `pets`, `pet_command`, `pet_swarm_query`. Each gives nil before the plugin has seen an
NMS server and outside the game. The windows keep their own settings in the pack's settings (`nms.loot.*`).

## Its files

| File | What |
|---|---|
| `plugins/nms/nms.ini` | `[NMS] skip_servers` (a comma list, default `triune`: the servers it leaves to their own plugin), `auth_key` (the server's Custom:ServerAuthKey; default the current patch's, 352236586; 0 sends no OP_CAuth), `hardware_id = fixed \| none` (the login checksum rewrite), `xtarget_hide_pets = 1 \| 0` |
| `servers/<profile>/server.ini` `[NMS]` | `auth_key`, `hardware_id` as builds before the plugin kept them for a server; read, over the plugin's own |
| `servers/<profile>/plugin-config/nms/nms.ini` | the same keys for one server, over both |
| `servers/<profile>/characters/<world>/<character>/plugin-config/nms/itemrules.ini` | the character's Personal Loot rules (the patch's `<name>_itemrules.ini`); one in the character's folder from before the plugin is moved here once |

## Changes from the built-in module

Until the plugin interface could do all of this, the same code was compiled into the client
(`src/plugins/nms`). Moving it out changed:

- The login window's Module box (detect / NMS / none) is gone. The plugin detects the server, and a player who does
  not want it on an NMS server does not install it. With "NMS server" forced, the module also rewrote the first login
  checksums, before the world had sent OP_SendCharacterSets. The plugin rewrites only after them, as the module's
  "detect" setting did.
- `hailstone.ini`'s `nms.auth_key`, `nms.hardware_id` and `nms.xtarget_hide_pets` are not read: they are
  `plugins/nms/nms.ini`'s now.
- The ImGui fallback windows (Personal Loot's Loot tab and the expansion-filtered waypoint list, for a UI without
  the Lua ones) are gone. The waypoint window is Lua in both packs, without the patch's SIDL layout.
- The unanswered client check sends OP_CAuth again. The module re-ran the client's whole last zone-in step.
- `eq.Nms*` became `eq.Plugin("nms").*`. A user script that called `eq.Nms*` must change.
