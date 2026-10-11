#pragma once
#include <stddef.h>
#include <stdint.h>
// Which received packets keep their raw copy as the route of an offline-queued
// message, and which app sessions are handed that route. Defaults are weak.
namespace offline_route {
  // At capture: payload type of the flood-routed packet that produced a queued message.
  bool keep(uint8_t payload_type);
  // At delivery: payload type of the stored route, and the bytes the app sent in
  // CMD_APP_START after the 8-byte header (app name and its capability list).
  bool deliver(uint8_t payload_type, const uint8_t* app_info, size_t app_info_len);
}
