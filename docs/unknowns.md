# NMS plugin: known gaps

Moved from the client's docs/unknowns.md on 2026-11-02. Same rule as there: what the plugin does now, and
what would settle it; remove a row when it is settled.

- The NMS add-on windows (this plugin): personal loot with its rules, history and settings, and the vault are in both packs (the gamepad ones redone 2026-10-29, d60f543). Not ported: the patch's `/browser` and `/weblink` (OP_Weblink goes to the core, which opens it: docs/browser.md), the spell-icon overlay (the patch's `SpellIcons\%d.png`): what the patch draws with it is not traced in its DLL.
- The Pet window's `#petcmd` switches light by the server's focused pet (OP_PetCommandState, sent only for
  `focused_pet_id`). With All, the other pets may be in another state (a pet summoned since, or one a class
  command changed); another class's pets and Assist light nothing. A server message, or a packet, that reported
  every pet's switches would settle it. Not tried live.
