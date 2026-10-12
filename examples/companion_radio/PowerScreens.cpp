#include "PowerScreens.h"

#include <Arduino.h>
#include <helpers/BatteryPack.h>

// 3% of a Li-ion: below ~3.56 V on the profile's curve (BatteryPack).
#ifndef AUTO_SHUTDOWN_PERCENT
  #define AUTO_SHUTDOWN_PERCENT  3
#endif

#ifndef POWER_ON_HINT
  #define POWER_ON_HINT "Press RST to turn on"
#endif
// After a low-battery stop: boards with nRF52 power management wake by
// themselves when USB power comes (see NRF52Board::configureVoltageWake).
#ifndef CHARGE_HINT
  #if defined(NRF52_POWER_MANAGEMENT)
    #define CHARGE_HINT "Plug in USB to turn on"
  #else
    #define CHARGE_HINT "Charge, then press RST"
  #endif
#endif

namespace power_screens {

void drawBoot(DisplayDriver* d) {
  if (!d) return;
  d->startFrame();
#ifdef ST7789
  d->setTextSize(2);
#endif
  d->drawTextCentered(d->width() / 2, 28, "Loading...");
  d->endFrame();
}

void drawPowerOff(DisplayDriver* d, bool low_battery) {
  if (!d) return;
  if (!d->isOn()) d->turnOn();
  const char* hint = low_battery ? CHARGE_HINT : POWER_ON_HINT;
  const int big = (d->width() >= 200) ? 2 : 1;
  const int lh  = d->getLineH(1);
  const int bh  = d->getLineH(big);
  const int total = (low_battery ? lh + 2 : 0) + bh + 4 + lh;
  int y = (d->height() - total) / 2;
  if (y < 0) y = 0;
  d->startFrame();
  d->setColor(UIColor::primary_txt);
  d->setTextSize(1);
  if (low_battery) { d->drawTextCentered(d->width() / 2, y, "Low battery"); y += lh + 2; }
  d->setTextSize(big);
  d->drawTextCentered(d->width() / 2, y, "Turned off");
  y += bh + 4;
  d->setTextSize(1);
  d->drawTextCentered(d->width() / 2, y, hint);
  d->endFrame();
  if (d->isEink()) {
    d->fullRefreshAndHibernate();   // keep the frame on the panel
  } else {
    delay(3000);                    // a lit screen goes dark with the node
  }
}

bool batteryFlat(uint16_t mv, bool external_power) {
  if (external_power || mv < 2500) return false;
  if (AUTO_SHUTDOWN_PERCENT > 0 && BatteryPack::getPercent(mv) <= AUTO_SHUTDOWN_PERCENT) return true;
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (mv < AUTO_SHUTDOWN_MILLIVOLTS) return true;
#endif
  return false;
}

}  // namespace power_screens
