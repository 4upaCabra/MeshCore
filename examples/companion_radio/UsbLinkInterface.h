#pragma once

#include <helpers/ArduinoSerialInterface.h>

#ifndef USB_LINK_IDLE_MS
  #define USB_LINK_IDLE_MS  (30UL * 60 * 1000)
#endif

// The app link over a USB cable, as one of the uni transports. A plain
// ArduinoSerialInterface reports "connected" whenever it is enabled, and on
// boards with a USB-UART bridge (CP210x) a computer cannot be told from a
// charger. Saying connected without an app would hide the alerts on screen and
// change how the offline queue is handled, so the link counts as connected
// only while an app talks: from its first frame until it goes quiet.
class UsbLinkInterface : public ArduinoSerialInterface {
  uint32_t _last_rx = 0;
  bool _seen = false;

public:
  bool isConnected() const override {
    return isEnabled() && _seen && (millis() - _last_rx) < USB_LINK_IDLE_MS;
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    size_t n = ArduinoSerialInterface::checkRecvFrame(dest);
    if (n > 0) {
      _seen = true;
      _last_rx = millis();
    }
    return n;
  }

  void disable() override {
    ArduinoSerialInterface::disable();
    _seen = false;
  }
};
