// The NMS plugin's decoders, included by the client's tests/fuzz_decoders.inc when this plugin is checked out
// (HAILSTONE_HAS_NMS). Keep in step with nms_packets.h.
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_character_sets)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_discipline_timer)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_loot_filters)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_loot_history)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_loot_settings)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_personal_loot_list)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_pet_command_state)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_pet_list)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_server_auth_stats)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_skill_timers)
HAILSTONE_FUZZ_PLUGIN_DECODER(nms, decode_waypoint_list)
