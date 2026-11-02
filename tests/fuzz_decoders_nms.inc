// The NMS plugin's decoders, included by the client's tests/fuzz_decoders.inc when this plugin is checked out
// (NMSC_HAS_NMS). Keep in step with nms_packets.h.
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_character_sets)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_discipline_timer)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_loot_filters)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_loot_history)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_loot_settings)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_personal_loot_list)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_pet_list)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_server_auth_stats)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_skill_timers)
NMSC_FUZZ_PLUGIN_DECODER(nms, decode_waypoint_list)
