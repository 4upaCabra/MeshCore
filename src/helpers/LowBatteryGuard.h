#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#ifndef LOWBATT_CHECK_MS
  #define LOWBATT_CHECK_MS       10000UL
#endif

#ifndef LOWBATT_STRIKES
  #define LOWBATT_STRIKES        6
#endif

#ifndef LOWBATT_BOOT_HYST_MV
  #define LOWBATT_BOOT_HYST_MV   150
#endif

#ifndef LOWBATT_ESP32_SLEEP_S
  #define LOWBATT_ESP32_SLEEP_S  1800
#endif

using mesh::MainBoard;

class LowBatteryGuard {
private:
  uint32_t last_check_ms = 0;
  uint8_t strikes = 0;

public:
  LowBatteryGuard() = default;

  // Samples voltage periodically; returns true once consecutive low readings
  // reach the strike limit.
  bool due(MainBoard& board, uint16_t shutdown_mv) {
    uint32_t now = millis();
    if ((uint32_t)(now - last_check_ms) < LOWBATT_CHECK_MS) {
      return false;
    }
    last_check_ms = now;

    if (shutdown_mv == 0 || board.isExternalPowered()) {
      strikes = 0;
      return false;
    }

    uint16_t mv = board.getBattMilliVolts();
    if (mv < 1000) {
      strikes = 0;
      return false;
    }

    if (mv < shutdown_mv) {
      strikes++;
      if (strikes >= LOWBATT_STRIKES) {
        strikes = 0;
        return true;
      }
    } else {
      strikes = 0;
    }

    return false;
  }

  // Periodic loop helper: invokes lowVoltageShutdown() on the board when due.
  void loop(MainBoard& board, uint16_t shutdown_mv) {
    if (due(board, shutdown_mv)) {
      board.lowVoltageShutdown();
    }
  }
};
