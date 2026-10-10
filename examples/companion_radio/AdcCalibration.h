#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NodePrefs.h>

// Pack voltage at 100% — same default the battery indicator uses.
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

// Battery-ADC calibration from the device menu (Settings → Cal ADC).
//
// The user charges the device until full and opens the item; we measure the
// divider, assume the pack sits at BATT_MAX_MILLIVOLTS (4.20 V for Li-ion) and
// rescale the multiplier so the reading lands there. mV = k × raw on every
// board that supports a multiplier, so the new k is simply
// old_k × target / measured — no raw ADC access needed.
//
// A fully charged cell is 4.20 V only while the charger holds it in CV; after
// termination it relaxes towards ~4.1 V. So this is a coarse calibration. A
// precise one (multimeter) stays in the CLI: 'set adc.multiplier'.
//
// The UI owns the screen and the keys; this struct only measures and applies.
struct AdcCalibration {
  enum Status : uint8_t { OK, NO_READING, UNSTABLE, OUT_OF_RANGE };
  enum Action : uint8_t { CANCEL, APPLY, RESET, ACTION_COUNT };
  enum Result : uint8_t { APPLIED, RESTORED, NO_EFFECT, CANCELLED };

  static const int   ROUNDS        = 8;      // separate board reads, spread checked
  static const int   MAX_SPREAD_MV = 80;     // more = charger pulsing / no battery
  static constexpr float MAX_STEP  = 0.15f;  // refuse a correction above ±15%

  uint16_t mv        = 0;      // reading with the current multiplier
  uint16_t target_mv = 0;      // what it should read
  float    old_k = 0.0f, new_k = 0.0f;
  Status   status = NO_READING;
  uint8_t  sel = CANCEL;       // highlighted action; Cancel first, so a stray click is harmless

  // Only boards that scale the reading by a multiplier report one.
  static bool supported(mesh::MainBoard& board) { return board.getAdcMultiplier() > 0.0f; }

  // USB presence is only detected on nRF52; elsewhere isExternalPowered() is
  // always false and a warning would be noise.
  static bool usbKnown() {
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
    return true;
#else
    return false;
#endif
  }

  // Average of several reads. 0 when the board gave nothing usable.
  static uint16_t readFresh(mesh::MainBoard& board, uint16_t* spread = nullptr) {
    uint32_t sum = 0;
    uint16_t lo = 0xFFFF, hi = 0;
    int n = 0;
    for (int r = 0; r < ROUNDS; r++) {
      uint16_t v = board.getBattMilliVolts();
      if (v > 0) {
        sum += v; n++;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      delay(5);
    }
    if (spread) *spread = n ? (uint16_t)(hi - lo) : 0;
    return n ? (uint16_t)(sum / n) : 0;
  }

  void measure(mesh::MainBoard& board) {
    sel = CANCEL;
    old_k = board.getAdcMultiplier();
    uint16_t spread = 0;
    mv = readFresh(board, &spread);
    target_mv = BATT_MAX_MILLIVOLTS;

    new_k = old_k;
    if (mv == 0) { status = NO_READING; return; }
    if (spread > MAX_SPREAD_MV) { status = UNSTABLE; return; }
    float ratio = (float)target_mv / (float)mv;
    if (ratio < 1.0f - MAX_STEP || ratio > 1.0f + MAX_STEP) { status = OUT_OF_RANGE; return; }
    new_k = old_k * ratio;
    status = OK;
  }

  bool canApply() const { return status == OK; }

  // Next / previous action, skipping Apply when there is nothing valid to apply.
  void step(bool fwd) {
    do {
      sel = fwd ? (sel + 1) % ACTION_COUNT : (sel + ACTION_COUNT - 1) % ACTION_COUNT;
    } while (sel == APPLY && !canApply());
  }

  static const char* actionName(uint8_t a) {
    switch (a) {
      case APPLY: return "Apply";
      case RESET: return "Default";
      default:    return "Cancel";
    }
  }

  const char* statusText() const {
    switch (status) {
      case NO_READING:   return "No reading";
      case UNSTABLE:     return "Unstable, no batt?";
      case OUT_OF_RANGE: return "Out of range";
      default:           return nullptr;
    }
  }

  // Runs the selected action. The caller saves prefs when it returns APPLIED
  // or RESTORED. Apply is verified by measuring again: a board that stores the
  // multiplier but does not use it is rolled back instead of saved.
  Result run(mesh::MainBoard& board, NodePrefs& prefs) {
    if (sel == RESET) {
      prefs.adc_multiplier = 0.0f;
      board.setAdcMultiplier(0.0f);
      return RESTORED;
    }
    if (sel != APPLY || !canApply()) return CANCELLED;

    float saved = prefs.adc_multiplier;
    board.setAdcMultiplier(new_k);
    uint16_t check = readFresh(board);
    int err = (int)check - (int)target_mv;
    if (err < 0) err = -err;
    if (check == 0 || err > (int)target_mv * 3 / 100) {
      board.setAdcMultiplier(saved);
      return NO_EFFECT;
    }
    prefs.adc_multiplier = new_k;
    return APPLIED;
  }
};
