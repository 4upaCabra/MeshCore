#include "OfflineRoutePolicy.h"

#include <Packet.h>

// Default policy: channel payloads keep their route, and any app session is
// handed it.
#if defined(__GNUC__)
__attribute__((weak)) bool offline_route::keep(uint8_t payload_type) {
  return payload_type == PAYLOAD_TYPE_GRP_TXT || payload_type == PAYLOAD_TYPE_GRP_DATA;
}
__attribute__((weak)) bool offline_route::deliver(uint8_t payload_type,
                                                  const uint8_t* app_info,
                                                  size_t app_info_len) {
  (void)payload_type;
  (void)app_info;
  (void)app_info_len;
  return true;
}
#else
bool offline_route::keep(uint8_t payload_type) {
  return payload_type == PAYLOAD_TYPE_GRP_TXT || payload_type == PAYLOAD_TYPE_GRP_DATA;
}
bool offline_route::deliver(uint8_t payload_type, const uint8_t* app_info,
                            size_t app_info_len) {
  (void)payload_type;
  (void)app_info;
  (void)app_info_len;
  return true;
}
#endif
