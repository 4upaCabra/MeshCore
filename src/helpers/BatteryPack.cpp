#include <helpers/BatteryPack.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#endif

namespace BatteryPack {

// ---- OCV tables (1S cell mV → %). Descending voltage. ----

// Typical LiPo / Li-ion resting voltage against charge left (the widely used
// 5%-step chart), with 0% moved to 3.40 V, the highest threshold the companion
// UI offers for `shutdown_mv`: about 2% of the cell is left there on this
// chart, so the steps below are rescaled by those 2% and the gauge reaches zero
// as the node goes off. One measured discharge was tried instead, but it rested
// on a constant-load assumption and an unchecked voltage reading, and it read
// 3.8 V as 17% where a normal cell still holds about 40%.
static const BatteryOcvPoint kLithium[] = {
  {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
  {3980,  75}, {3950, 69}, {3910, 64}, {3870, 59}, {3850, 54},
  {3840,  49}, {3820, 44}, {3800, 39}, {3790, 34}, {3770, 29},
  {3750,  24}, {3730, 18}, {3710, 13}, {3690,  8}, {3610,  3},
  {3400,   0},
};

static const BatteryOcvPoint kLiFePO4[] = {
  {3650, 100}, {3450, 95}, {3400, 90}, {3350, 70},
  {3300,  40}, {3250, 20}, {3200, 10}, {3000,  5},
  {2700,   0},
};

static const BatteryOcvPoint kNiMH[] = {
  {1400, 100}, {1350, 90}, {1300, 70}, {1250, 40},
  {1200,  20}, {1100, 10}, {1000,  0},
};

static const BatteryOcvPoint kLeadAcid[] = {
  {2150, 100}, {2100, 90}, {2050, 70}, {2000, 40},
  {1950,  20}, {1900, 10}, {1800,  0},
};

static const BatteryProfile kProfiles[] = {
  {BatteryChemistry::Lithium,  1, kLithium,  (uint8_t)(sizeof(kLithium) / sizeof(kLithium[0]))},
  {BatteryChemistry::LiFePO4,  1, kLiFePO4,  (uint8_t)(sizeof(kLiFePO4) / sizeof(kLiFePO4[0]))},
  {BatteryChemistry::NiMH,     1, kNiMH,     (uint8_t)(sizeof(kNiMH) / sizeof(kNiMH[0]))},
  {BatteryChemistry::LeadAcid, 1, kLeadAcid, (uint8_t)(sizeof(kLeadAcid) / sizeof(kLeadAcid[0]))},
};

static uint8_t seriesCells() {
  uint8_t cells = (uint8_t)BATT_SERIES_CELLS;
  return cells < 1 ? 1 : cells;
}

const BatteryProfile& profileFor(BatteryChemistry chem) {
  for (unsigned i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); i++) {
    if (kProfiles[i].chem == chem) return kProfiles[i];
  }
  return kProfiles[0];  // Lithium fallback (Custom / unknown)
}

const BatteryProfile& activeProfile() {
  return profileFor(static_cast<BatteryChemistry>((uint8_t)BATT_CHEMISTRY));
}

static uint8_t percentFromTable(uint16_t cell_mv, const BatteryOcvPoint* pts, uint8_t n) {
  if (!pts || n == 0) return 0;
  if (cell_mv >= pts[0].mv) return pts[0].pct;
  if (cell_mv <= pts[n - 1].mv) return pts[n - 1].pct;

  for (uint8_t i = 0; i + 1 < n; i++) {
    const uint16_t mv_hi = pts[i].mv;
    const uint16_t mv_lo = pts[i + 1].mv;
    if (cell_mv <= mv_hi && cell_mv >= mv_lo) {
      const uint8_t pct_hi = pts[i].pct;
      const uint8_t pct_lo = pts[i + 1].pct;
      if (mv_hi == mv_lo) return pct_hi;
      // Linear interpolate between neighbouring OCV points.
      return (uint8_t)(pct_lo +
                       ((uint32_t)(cell_mv - mv_lo) * (uint32_t)(pct_hi - pct_lo)) /
                           (uint32_t)(mv_hi - mv_lo));
    }
  }
  return 0;
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem, uint8_t series_cells) {
  const BatteryProfile& prof = profileFor(chem);
  uint8_t cells = series_cells ? series_cells : seriesCells();
  if (cells < 1) cells = 1;
  const uint16_t cell_mv = (uint16_t)(pack_mv / cells);
  return percentFromTable(cell_mv, prof.points, prof.point_count);
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem) {
  return getPercent(pack_mv, chem, 0);
}

uint8_t getPercent(uint16_t pack_mv) {
  return getPercent(pack_mv, activeProfile().chem, 0);
}

uint16_t fullMilliVolts() {
  const BatteryProfile& prof = activeProfile();
  return (uint16_t)(prof.points[0].mv * seriesCells());
}

// ---- Optional pack-voltage cache (INA feed) ----

static uint16_t s_pack_mv = 0;
static uint32_t s_pack_ms = 0;
static bool s_pack_valid = false;

static uint32_t s_test_clock_ms = 0;
static uint32_t (*s_clock_ms)() = nullptr;

static uint32_t nowMs() {
  if (s_clock_ms) return s_clock_ms();
#if defined(ARDUINO)
  return millis();
#else
  return s_test_clock_ms;
#endif
}

void setClockMs(uint32_t (*clock_ms)()) { s_clock_ms = clock_ms; }

void setClockMsValue(uint32_t ms) { s_test_clock_ms = ms; }

void notePackMilliVolts(uint16_t mv) {
  // Ignore obvious garbage (open / short / wrong rail).
  if (mv < 500 || mv > 20000) return;
  s_pack_mv = mv;
  s_pack_ms = nowMs();
  s_pack_valid = true;
}

void clearPackMilliVolts() {
  s_pack_valid = false;
  s_pack_mv = 0;
  s_pack_ms = 0;
}

bool hasFreshPackMilliVolts(uint16_t& out_mv) {
  if (!s_pack_valid) return false;
  const uint32_t age = nowMs() - s_pack_ms;
  if (age > BATT_PACK_INA_TTL_MS) return false;
  out_mv = s_pack_mv;
  return true;
}

}  // namespace BatteryPack
