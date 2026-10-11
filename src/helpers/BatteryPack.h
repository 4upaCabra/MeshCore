#pragma once

#include <stdint.h>

// Open-circuit voltage (OCV) → remaining charge % for mesh companion UI / telemetry.
// Default chemistry is Lithium (LiPo / Li-ion 4.2 V). Other tables are available via
// BATT_CHEMISTRY (compile-time); runtime prefs/CLI can come later.
//
// Optional INA-first pack voltage: define BATT_PACK_INA=1 on the variant. Wiring
// (sensors.loop poll + board prefer) is behind that flag; the cache helpers below
// are always compiled so unit tests can cover them without the flag.

enum class BatteryChemistry : uint8_t {
  Lithium  = 0,  // LiPo / Li-ion 4.2 V
  LiFePO4  = 1,
  NiMH     = 2,
  LeadAcid = 3,
  Custom   = 255
};

struct BatteryOcvPoint {
  uint16_t mv;
  uint8_t  pct;
};

struct BatteryProfile {
  BatteryChemistry chem;
  uint8_t series_cells;
  const BatteryOcvPoint* points;
  uint8_t point_count;
};

#ifndef BATT_SERIES_CELLS
  #ifdef BATT_MAX_MILLIVOLTS
    #define BATT_SERIES_CELLS ((BATT_MAX_MILLIVOLTS) / 4200)
    #if BATT_SERIES_CELLS < 1
      #undef BATT_SERIES_CELLS
      #define BATT_SERIES_CELLS 1
    #endif
  #else
    #define BATT_SERIES_CELLS 1
  #endif
#endif

#ifndef BATT_CHEMISTRY
  #define BATT_CHEMISTRY ((uint8_t)BatteryChemistry::Lithium)
#endif

// Pack voltage cache TTL when fed from an INA (ms).
#ifndef BATT_PACK_INA_TTL_MS
  #define BATT_PACK_INA_TTL_MS 45000u
#endif

namespace BatteryPack {

const BatteryProfile& profileFor(BatteryChemistry chem);
const BatteryProfile& activeProfile();

// Piecewise-linear OCV lookup for the active (or given) profile.
// series_cells == 0 → use BATT_SERIES_CELLS.
uint8_t getPercent(uint16_t pack_mv);
uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem);
uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem, uint8_t series_cells);

// Pack voltage at 100% for the active profile (top OCV point × series cells).
uint16_t fullMilliVolts();

// INA / test feed — always available; unused unless something calls notePackMilliVolts.
void notePackMilliVolts(uint16_t mv);
bool hasFreshPackMilliVolts(uint16_t& out_mv);
void clearPackMilliVolts();

// Test clock override (native / unit tests). Pass nullptr to use millis() on Arduino.
void setClockMs(uint32_t (*clock_ms)());
void setClockMsValue(uint32_t ms);  // absolute time for the default test clock

}  // namespace BatteryPack
